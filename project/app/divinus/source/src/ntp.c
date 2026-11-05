/* OKO: NTP control for the WebUI (see ntp.h).  The image ships the reference
 * ntpd and starts it from /etc/init.d/S49ntp; divinus only takes the daemon
 * over for a custom server or when NTP is switched off. */

#include "ntp.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app_config.h"
#include "hal/macros.h"

#define NTP_CONF "/tmp/divinus-ntp.conf"
#define NTP_PID "/tmp/divinus-ntpd.pid"

bool ntp_tool_available(void) {
    return access(NTP_BIN, X_OK) == 0;
}

bool ntp_running(void) {
    FILE *pipe = popen("pidof ntpd", "r");
    char buf[64];
    bool running = false;

    if (!pipe) return false;
    if (fgets(buf, sizeof(buf), pipe))
        running = buf[0] >= '0' && buf[0] <= '9';
    pclose(pipe);
    return running;
}

/* Starts the daemon in the background.  An empty server keeps the
 * distribution configuration (/etc/ntp.conf); a configured server uses a
 * generated file so the rootfs stays untouched. */
static int ntp_start(const char *server) {
    if (!server || !*server)
        return system(NTP_BIN " -g -p /var/run/ntpd.pid >/dev/null 2>&1 &") == 0 ? 0 : -1;

    FILE *conf = fopen(NTP_CONF, "w");
    if (!conf) return -1;
    fprintf(conf, "server %s iburst\n", server);
    fprintf(conf, "restrict default nomodify nopeer noquery limited kod\n");
    fprintf(conf, "restrict 127.0.0.1\n");
    fclose(conf);

    return system(NTP_BIN " -g -c " NTP_CONF " -p " NTP_PID
        " >/dev/null 2>&1 &") == 0 ? 0 : -1;
}

static void ntp_stop(void) {
    system("killall -q ntpd");
}

int ntp_apply(void) {
    if (!ntp_tool_available())
        return -1;

    ntp_stop();
    if (!app_config.ntp_enable)
        return 0;

    /* Let the stopped daemon release port 123 before the replacement binds. */
    usleep(300000);
    return ntp_start(app_config.ntp_server);
}

void ntp_apply_startup(void) {
    if (!ntp_tool_available())
        return;

    if (!app_config.ntp_enable) {
        ntp_stop();
        return;
    }

    if (!app_config.ntp_server[0]) {
        /* S49ntp normally started the vendor daemon; if it did not (or was
         * stopped by a previous disable), bring it back. */
        if (!ntp_running()) {
            HAL_INFO("ntp", "Starting the distribution ntpd\n");
            ntp_start(NULL);
        }
        return;
    }

    ntp_stop();
    usleep(300000);
    if (ntp_start(app_config.ntp_server))
        HAL_WARNING("ntp", "Could not start ntpd with server '%s'\n",
            app_config.ntp_server);
}

static void *ntp_startup_worker(void *arg) {
    (void)arg;
    /* S21appinit (RkLunch) starts divinus before S49ntp starts the vendor
     * daemon; give the init script time to finish before taking over. */
    sleep(10);
    ntp_apply_startup();
    return NULL;
}

void ntp_startup_schedule(void) {
    pthread_t thread;

    if (!ntp_tool_available())
        return;
    if (app_config.ntp_enable && !app_config.ntp_server[0])
        return;

    if (pthread_create(&thread, NULL, ntp_startup_worker, NULL) != 0)
        HAL_WARNING("ntp", "Could not schedule the NTP startup check\n");
    else
        pthread_detach(thread);
}