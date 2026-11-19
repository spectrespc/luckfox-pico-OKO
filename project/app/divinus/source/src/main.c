#include "app_config.h"
#include "firmware_status.h"
#include "focus.h"
#include "focus_motor.h"
#include "focus_proto.h"
#include "gating.h"
#include "hal/macros.h"
#include "heartbeat_task.h"
#include "http_post.h"
#include "mqtt_post.h"
#include "record_clip.h"
#include "ivs_events.h"
#include "media.h"
#include "network.h"
#include "night.h"
#include "ntp.h"
#include "rtsp/rtsp_server.h"
#include "server.h"
#include "sleep_policy.h"
#include "watchdog.h"
#include "analytics/models.h"
#include "analytics/yolo.h"

#include <fcntl.h>
#include <sys/resource.h>

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

rtsp_handle rtspHandle;
volatile char graceful = 0, keepRunning = 1;

void handle_error(int signo) {
    char msg[64];
    sprintf(msg, "Error occured (%d)! Quitting...\n", signo);
    write(STDERR_FILENO, msg, strlen(msg));
    keepRunning = 0;
    /* _exit(), not exit(): from a signal handler only async-signal-safe
       calls are allowed, and skipping the atexit handlers leaves the
       watchdog armed so a crash still ends in a reset */
    _exit(EXIT_FAILURE);
}

void handle_exit(int signo) {
    write(STDERR_FILENO, "Graceful shutdown...\n", 21);
    keepRunning = 0;
    graceful = (signo == SIGHUP);
}

int main(int argc, char *argv[]) {
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = handle_error;
        sigaction(SIGABRT, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
        sigaction(SIGFPE, &sa, NULL);
        sigaction(SIGILL, &sa, NULL);
        sigaction(SIGSEGV, &sa, NULL);

        sa.sa_handler = handle_exit;
        sigaction(SIGHUP, &sa, NULL);
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGQUIT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);

        sa.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &sa, NULL);
    }

    hal_identify();

    if (!*family)
        HAL_ERROR("hal", "Unsupported chip family! Quitting...\n");

    fprintf(stderr, "\033[0m\033[7m Divinus (rev %s) for %s \033[0m\n", GIT_REV, family);
    fprintf(stderr, "Chip ID: %s\n", chip);

    {
        enum ConfigError configError = app_config_parse();
        if (configError != CONFIG_OK) {
            /* OKO: every save keeps the previous file as divinus.yaml.bak.
             * An imported or hand-edited file that no longer parses must not
             * leave the device without a streamer, so roll back once. */
            HAL_WARNING("hal", "Configuration error %d; restoring the backup\n",
                configError);
            app_config_restore();
            configError = app_config_parse();
        }
        if (configError != CONFIG_OK) {
            fprintf(stderr, "Configuration error code: %d\n", configError);
            HAL_ERROR("hal", "Can't load app config 'divinus.yaml'\n");
        }
    }

    /* Unsafe combinations are refused even when edited directly in the YAML. */
    if (app_config.aiisp_enable != HAL_AIISP_OFF && app_config.hdr_mode != 0) {
        HAL_WARNING("hal", "AIISP and HDR cannot run together; AIISP disabled\n");
        app_config.aiisp_enable = HAL_AIISP_OFF;
    }
    if (app_config.hdr_mode != 0 && app_config.hdr_apply == 1) {
        HAL_WARNING("hal", "Dynamic HDR is unavailable; restart mode used\n");
        app_config.hdr_apply = 2;
    }
    /* VI channel 1 (selfpath) is wired up in the ISP topology but enabling it
     * hangs this board, so a hand-edited YAML must not reach the HAL. */
    if (app_config.yolo_source == 0) {
        HAL_WARNING("hal", "yolo.source=vi1 hangs the RV1106 board; "
            "falling back to vpss3\n");
        app_config.yolo_source = 1;
    }
    /* VPSS channel 3 is shared by MJPEG and yolo.source=vpss3; IVS is on its
     * own VI channel 2 and does not compete.  A hand-edited YAML must not
     * leave the analytics without a frame source. */
    if (app_config.yolo_enable && app_config.yolo_source == 1 &&
        app_config.mjpeg_enable) {
        HAL_WARNING("hal", "yolo.source=vpss3 conflicts with MJPEG; "
            "falling back to vi0\n");
        app_config.yolo_source = 2;
    }
    focus_motor_init(app_config.focus_device, app_config.focus_address);
    focus_motor_set_enabled(app_config.focus_motor);
    if (app_config.focus_motor) {
        focus_motor_set_speed(FOCUS_MOTOR_FOCUS, app_config.focus_speed);
        focus_motor_set_speed(FOCUS_MOTOR_ZOOM, app_config.focus_zoom_speed);
    }

    /* OKO: read [sleep] and the OKO_SLEEP_WAKE probe flag before any media
     * subsystem is started; the policy thread itself starts after IVS/YOLO.
     * heartbeat_init() must run first: sleep_policy_init() consumes the wake
     * marker/env that the heartbeat module also needs. */
    heartbeat_init();
    sleep_policy_init();

    models_start();
    if (app_config.watchdog)
        watchdog_start(app_config.watchdog);

    network_start();

    /* OKO: honour ntp_enable / ntp_server from the saved configuration. */
    ntp_startup_schedule();

    server_start();

    if (app_config.rtsp_enable) {
        rtspHandle = rtsp_create(RTSP_MAXIMUM_CONNECTIONS, app_config.rtsp_port, 1);
        rtsp_latch_audio_codec();
        HAL_INFO("rtsp", "Started listening for clients...\n");
        if (app_config.rtsp_enable_auth) {
            if (EMPTY(app_config.rtsp_auth_user) || EMPTY(app_config.rtsp_auth_pass))
                HAL_ERROR("rtsp", "One or both credential fields have been left empty!\n");
            else {
                rtsp_configure_auth(rtspHandle, app_config.rtsp_auth_user, app_config.rtsp_auth_pass);
                HAL_INFO("rtsp", "Authentication enabled!\n");
            }
        }
        if (app_config.substream_enable)
            rtsp_add_stream(rtspHandle, app_config.substream_path);
    }

    if (app_config.stream_enable)
        media_start();

    if (sdk_start())
        HAL_ERROR("hal", "Failed to start SDK!\n");

    if (app_config.focus_enable) {
        gating_result gate = gating_check("focus_metric");
        if (!gate.ok)
            HAL_WARNING("focus", "Focus assistant blocked: %s\n", gate.reason);
        else if (focus_start()) {
            app_config.focus_enable = false;
            HAL_WARNING("focus", "Focus statistics could not be started\n");
        }
    }

    firmware_status_start();

    if (app_config.ivs_enable) {
        gating_result gate = gating_check("ivs");
        if (gate.ok)
            ivs_events_start();
        else
            HAL_WARNING("ivs", "Analytics blocked: %s\n", gate.reason);
    }

    if (app_config.event_clip_enable) {
        gating_result gate = gating_check("event_clip");

        if (!gate.ok) {
            app_config.event_clip_enable = false;
            HAL_WARNING("clip", "Event clips blocked: %s (%s)\n",
                gate.reason, gate.hint ? gate.hint : "no hint");
        } else if (record_clip_start()) {
            app_config.event_clip_enable = false;
            HAL_WARNING("clip", "Event clip service could not be started\n");
        }
    }

    if (app_config.yolo_enable) {
        gating_result gate = gating_check("yolo");
        if (gate.ok) {
            if (yolo_start())
                HAL_WARNING("yolo", "Detection could not be started\n");
        } else {
            HAL_WARNING("yolo", "Analytics blocked: %s (%s)\n",
                gate.reason, gate.hint ? gate.hint : "no hint");
        }
    }

    if (app_config.night_mode_enable)
        night_enable();

    if (app_config.http_post_enable)
        http_post_start();

    /* [mqtt]: a "none" host would only keep the reconnect timer busy, so the
     * thread starts once a real broker is configured. */
    if (app_config.mqtt_enable && *app_config.mqtt_host &&
        !EQUALS_CASE(app_config.mqtt_host, "none"))
        mqtt_post_start();

    if (app_config.osd_enable)
        region_start();

    if (app_config.record_enable && app_config.record_continuous)
        record_start();

    sleep_policy_start();

    int sleepPending = 0;

    while (keepRunning) {
        if (sleep_policy_should_suspend()) {
            /* A quiet probe cycle is the only place a heartbeat snapshot is
             * sent.  It blocks the entry into suspend for at most
             * heartbeat_retries * (3 s connect + 1 s pause); a failure still
             * lets the camera sleep (the receiver notices the gap). */
            if (sleep_policy_cycle_is_quiet_resume() && heartbeat_due())
                heartbeat_before_suspend();
            HAL_INFO("sleep", "entering the sleep cycle\n");
            sleepPending = 1;
            graceful = 1;
            keepRunning = 0;
            break;
        }
        if (app_config.rtsp_enable) rtsp_tick(rtspHandle);
        network_tick();
        watchdog_reset();
        sleep(1);
    }

    /* The policy thread owns no media state, but it must be joined before the
     * subsystems it reports into are stopped. */
    sleep_policy_stop();

    if (app_config.record_enable && app_config.record_continuous)
        record_stop();

    if (app_config.http_post_enable)
        http_post_stop();

    /* Publishes the retained "online:false" status before closing. */
    mqtt_post_stop();

    if (app_config.osd_enable)
        region_stop();

    if (app_config.night_mode_enable)
        night_disable();

    yolo_stop();

    record_clip_stop();

    ivs_events_stop();

    focus_stop();
    firmware_status_stop();

    /*
     * AOV: the MCU takes frames from the CSI/ISP while the AP is suspended,
     * so the AOV profile can keep the pipeline up (KI-072).  Everywhere else
     * the pipeline is stopped: a live CSI/ISP stream across a mem cycle used
     * to turn the resume into a mipi-csi2 error storm.
     */
    int aovKeepPipeline = sleepPending && sleep_policy_aov_keep_pipeline();

    if (aovKeepPipeline)
        HAL_INFO("sleep", "AOV: keeping the CSI/ISP pipeline up for the MCU\n");

    if (!aovKeepPipeline)
        sdk_stop();

    if (app_config.stream_enable && !aovKeepPipeline)
        media_stop();

    if (app_config.rtsp_enable) {
        rtsp_finish(rtspHandle);
        HAL_INFO("rtsp", "Server has closed!\n");
    }

    server_stop();

    focus_motor_shutdown();

    network_stop();

    if (app_config.watchdog)
        watchdog_stop();

    /* OKO: the media pipeline is down (the bench showed that a live CSI/ISP
     * stream turns resume into a mipi-csi2 error storm), so it is safe to
     * suspend.  After PMU wake the process restarts through the normal
     * graceful path; OKO_SLEEP_WAKE asks the new instance for one short
     * motion-check window before it sleeps again. */
    if (sleepPending) {
        int slept = sleep_policy_suspend();
        if (slept == 0) {
            HAL_INFO("sleep", "suspend cycle done; restarting into wake probe\n");
            sleep_policy_mark_wake();
        } else {
            HAL_WARNING("sleep", "suspend cancelled or failed; restarting the process\n");
        }
    }

    if (!graceful)
        app_config_restore();

    if (graceful) {
        struct rlimit rl;
        int fd, marked = 0;
        int limit = 1024;               /* current bench ulimit -n */

        if (getrlimit(RLIMIT_NOFILE, &rl) == 0 &&
            rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < 65536)
            limit = (int)rl.rlim_cur;

        fprintf(stderr, "Restarting...\n");
        /* The vendor media stack keeps a few dmabuf and RGA descriptors open
         * at exit, and execvp() would inherit them, so every SIGHUP restart
         * leaked descriptors and CMA (KI-069).  Mark the rest close-on-exec
         * and let the kernel release them in the new image. */
        for (fd = 3; fd < limit; fd++) {
            if (fcntl(fd, F_SETFD, FD_CLOEXEC) != -1)
                marked++;
        }
        HAL_INFO("main", "restart: %d descriptor(s) closed on exec\n", marked);
        execvp(argv[0], argv);
    }

    fprintf(stderr, "Main thread is shutting down...\n");
    return EXIT_SUCCESS;
}
