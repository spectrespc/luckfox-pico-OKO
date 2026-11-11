#include "media.h"
#include "gating.h"

#include <time.h>

char audioOn = 0, udpOn = 0;
pthread_mutex_t aencMtx, chnMtx, mp4Mtx;
pthread_t aencPid = 0, audPid = 0, ispPid = 0, vidPid = 0;

struct BitBuf mp3Buf;
shine_config_t mp3Cnf;
shine_t mp3Enc;
unsigned int pcmPos, pcmSamp;
short pcmSrc[SHINE_MAX_SAMPLES];

/* Raw PCM queue between the capture callback and the MP3 encoder, so the
 * capture thread only ever copies a frame and the SDK never drops buffers
 * waiting on the encode.  Newest frames are dropped when it is full. */
static struct ringbuf audRing;
static unsigned int audDrops;
static void media_eptz_mark_applied(char bound);
static void media_rotate_mark_applied(char bound);

int save_audio_stream(hal_audframe *frame) {
#ifdef DEBUG_AUDIO
    printf("[audio] data:%p - %02x %02x %02x %02x %02x %02x %02x %02x\n",
        frame->data, frame->data[0][0], frame->data[0][1], frame->data[0][2], frame->data[0][3],
        frame->data[0][4], frame->data[0][5], frame->data[0][6], frame->data[0][7]);
    printf("        len:%d\n", frame->length[0]);
    printf("        seq:%d\n", frame->seq);
    printf("        ts:%d\n", frame->timestamp);
#endif

    static uint8_t pack[AUD_FRAME_MAX + 2];
    unsigned int len = frame->length[0] > AUD_FRAME_MAX ?
        AUD_FRAME_MAX : frame->length[0];
    pack[0] = (len >> 8) & 0xFF;
    pack[1] = len & 0xFF;
    memcpy(pack + 2, frame->data[0], len);

    if (ringbuf_put(&audRing, pack, len + 2))
        audDrops++;

    return EXIT_SUCCESS;
}

static unsigned char pcm_to_alaw(short pcm)
{
    int sign = (pcm & 0x8000) >> 8, s = sign ? -pcm - 1 : pcm, exp = 7;
    if (s > 32635) s = 32635;
    if (s >= 256) {
        for (int m = 0x4000; !(s & m) && exp > 0; m >>= 1) exp--;
        s = ((exp << 4) | ((s >> (exp + 3)) & 0x0f));
    } else
        s >>= 4;
    return (unsigned char)((s ^ (sign ? 0xd5 : 0x55)));
}

/* mu-law (G.711 U, PT 0): 4085-step non-uniform compressor. Mirrors the A-law
 * routine so both stay branch-light for soft-float ARM. */
static unsigned char pcm_to_ulaw(short pcm)
{
    int sign = (pcm >> 8) & 0x80, s = sign ? -pcm : pcm, exp = 7;
    if (s > 32635) s = 32635;
    s += 0x84;                         /* bias: mu-law does not encode zero alone */
    if (s > 0x7FFF) s = 0x7FFF;
    for (int m = 0x4000; !(s & m) && exp > 0; m >>= 1) exp--;
    return (unsigned char)~(sign | (exp << 4) | ((s >> (exp + 3)) & 0x0f));
}

static char rtsp_codec[sizeof(app_config.rtsp_audio_codec)];

/* Any G.711 codec: the 8 kHz sample drop + encode path, versus MP3 */
static char rtsp_g711_active(void) { return EQUALS(rtsp_codec, "pcma") || EQUALS(rtsp_codec, "pcmu"); }

static void rtsp_pcma_feed(short *pcm, unsigned int samples)
{
    static unsigned char alaw[160];
    static unsigned int fill, phase, step;
    static int acc, count;
    char ulaw = EQUALS(rtsp_codec, "pcmu");
    if (!step) {
        unsigned int srate = app_config.audio_srate > 0 ? app_config.audio_srate : 8000;
        step = (srate << 16) / 8000;
        if (!step) step = 1 << 16;
    }
    for (unsigned int i = 0; i < samples; i++) {
        acc += pcm[i];
        count++;
        phase += 1 << 16;
        if (phase < step) continue;
        phase -= step;
        alaw[fill++] = ulaw ? pcm_to_ulaw((short)(acc / count)) : pcm_to_alaw((short)(acc / count));
        acc = 0; count = 0;
        if (fill == sizeof(alaw)) {
            if (ulaw)
                rtp_send_pcmu(rtspHandle, alaw, sizeof(alaw));
            else
                rtp_send_pcma(rtspHandle, alaw, sizeof(alaw));
            fill = 0;
        }
    }
}

void rtsp_latch_audio_codec(void) {
    strncpy(rtsp_codec, app_config.rtsp_audio_codec, sizeof(rtsp_codec) - 1);
    rtsp_codec[sizeof(rtsp_codec) - 1] = 0;
}

void *aenc_thread(void) {
    static uint8_t frame_buf[AUD_FRAME_MAX + 2];
    const uint32_t mp3FrmSize =
        (app_config.audio_srate >= 32000 ? 144 : 72) *
        (app_config.audio_bitrate * 1000) /
        app_config.audio_srate;
    uint8_t hdr[2];
    unsigned int seq = 0;
    int ret;

    while (keepRunning && audioOn) {
        pthread_mutex_lock(&aencMtx);
        if (mp3Buf.offset >= mp3FrmSize) {
            send_mp3_to_client(mp3Buf.buf, mp3FrmSize);
            pthread_mutex_lock(&mp4Mtx);
            mp4_ingest_audio(mp3Buf.buf, mp3FrmSize);
            pthread_mutex_unlock(&mp4Mtx);
            if (app_config.rtsp_enable && !rtsp_g711_active())
                rtp_send_mp3(rtspHandle, mp3Buf.buf, mp3FrmSize);
            rtmp_ingest_audio(mp3Buf.buf, mp3FrmSize);
            mp3Buf.offset -= mp3FrmSize;
            if (mp3Buf.offset > 0)
                memmove(mp3Buf.buf, mp3Buf.buf + mp3FrmSize, mp3Buf.offset);
        }
        pthread_mutex_unlock(&aencMtx);

        if (ringbuf_peek(&audRing, hdr, 2)) {
            usleep(5000);
            continue;
        }
        unsigned int flen = (hdr[0] << 8) | hdr[1];
        if (ringbuf_used(&audRing) < 2 + flen) {
            usleep(5000);
            continue;
        }
        ringbuf_get(&audRing, frame_buf, 2 + flen);

        hal_audframe outFrame;
        outFrame.channelCnt = 1;
        outFrame.data[0] = (unsigned char *)frame_buf + 2;
        outFrame.length[0] = flen;
        outFrame.seq = seq++;
        outFrame.timestamp = millis();
        send_pcm_to_client(&outFrame);
        if (app_config.rtsp_enable && rtsp_g711_active())
            rtsp_pcma_feed((short *)(frame_buf + 2), flen / 2);

        if (!record_active() && !app_config.stream_enable && !any_http_audio() &&
            !(app_config.rtsp_enable && !rtsp_g711_active())) {
            pcmPos = 0;
            continue;
        }

        unsigned int pcmLen = flen / 2;
        short *pcmPack = (short *)(frame_buf + 2);
        short *srcPtr = pcmPack + pcmLen;

        while (pcmPos + pcmLen >= pcmSamp) {
            int copyLen = pcmSamp - pcmPos;
            memcpy(pcmSrc + pcmPos, srcPtr - pcmLen, copyLen * 2);
            unsigned char *mp3Ptr = shine_encode_buffer_interleaved(mp3Enc, pcmSrc, &ret);
            pthread_mutex_lock(&aencMtx);
            put(&mp3Buf, mp3Ptr, ret);
            pthread_mutex_unlock(&aencMtx);
            pcmLen -= copyLen;
            pcmPos = 0;
        }

        if (pcmLen > 0)
            memcpy(pcmSrc + pcmPos, srcPtr - pcmLen, pcmLen * 2);
        pcmPos += pcmLen;
    }
    HAL_INFO("media", "Shutting down audio encode thread (%u dropped)...\n", audDrops);
    return NULL;
}

int save_video_stream(char index, hal_vidstream *stream) {
    int ret;

    switch (chnState[index].payload) {
        case HAL_VIDCODEC_H264:
        case HAL_VIDCODEC_H265:
        {
            char isH265 = chnState[index].payload == HAL_VIDCODEC_H265 ? 1 : 0;
            int role = chnState[index].role;

            if (role == HAL_CHNROLE_SUB) {
                /* The sub stream is RTSP-only and, optionally, the recording
                 * source; it never feeds the MP4 client or RTMP/UDP. */
                if (app_config.rtsp_enable)
                    rtp_send_h26x(rtspHandle, 1, stream, isH265);
                if (record_wants_source(true))
                    record_ingest_stream(stream, isH265);
                break;
            }

            if (app_config.mp4_enable) {
                pthread_mutex_lock(&mp4Mtx);
                send_mp4_to_client(index, stream, isH265);
                pthread_mutex_unlock(&mp4Mtx);

                send_h26x_to_client(index, stream);
            }
            if (app_config.rtsp_enable)
                rtp_send_h26x(rtspHandle, 0, stream, isH265);

            if (record_wants_source(false))
                record_ingest_stream(stream, isH265);

            if (app_config.stream_enable) {
                /* One NAL unit per RTP packet.  A pack holds Annex-B data that
                 * may contain several NAL units (AUD/SPS/PPS/slices); sending
                 * the pack as a single payload, start codes included, is not
                 * depacketizable by any standard receiver. */
                int nal_total = 0;
                for (int i = 0; i < stream->count; i++) {
                    hal_vidpack *pack = &stream->pack[i];
                    unsigned char *pack_data;
                    size_t pack_len, nal_len = 0;
                    unsigned char *nalptr;

                    if (pack->length <= pack->offset) continue;
                    pack_data = pack->data + pack->offset;
                    pack_len = pack->length - pack->offset;
                    nalptr = pack_data;
                    while (nal_split(pack_data, &nalptr, &nal_len, pack_len) == 0)
                        if (nal_len) nal_total++;
                }

                int nal_index = 0;
                for (int i = 0; i < stream->count; i++) {
                    hal_vidpack *pack = &stream->pack[i];
                    unsigned char *pack_data;
                    size_t pack_len, nal_len = 0;
                    unsigned char *nalptr;

                    if (pack->length > pack->offset) {
                        pack_data = pack->data + pack->offset;
                        pack_len = pack->length - pack->offset;
                        nalptr = pack_data;
                        while (nal_split(pack_data, &nalptr, &nal_len, pack_len) == 0) {
                            if (!nal_len) continue;
                            /* The RTP marker belongs on the last packet of the
                             * access unit, not on keyframe slices. */
                            udp_stream_send_nal((const char *)nalptr, (int)nal_len,
                                ++nal_index == nal_total, isH265);
                        }
                    }
                    rtmp_ingest_video(pack, isH265);
                }

                if (nal_total)
                    udp_stream_frame_end(90000 /
                        (app_config.mp4_fps ? app_config.mp4_fps : 30));
            }
            break;
        }
        case HAL_VIDCODEC_MJPG:
            if (app_config.mjpeg_enable) {
                static char *mjpeg_buf;
                static ssize_t mjpeg_buf_size = 0;
                ssize_t buf_size = 0;
                for (unsigned int i = 0; i < stream->count; i++) {
                    hal_vidpack *data = &stream->pack[i];
                    ssize_t need_size = buf_size + data->length - data->offset + 2;
                    if (need_size > mjpeg_buf_size)
                        mjpeg_buf = realloc(mjpeg_buf, mjpeg_buf_size = need_size);
                    memcpy(mjpeg_buf + buf_size, data->data + data->offset,
                        data->length - data->offset);
                    buf_size += data->length - data->offset;
                }
                send_mjpeg_to_client(index, mjpeg_buf, buf_size);
            }
            break;
        case HAL_VIDCODEC_JPG:
        {
            static char *jpeg_buf;
            static ssize_t jpeg_buf_size = 0;
            ssize_t buf_size = 0;
            for (unsigned int i = 0; i < stream->count; i++) {
                hal_vidpack *data = &stream->pack[i];
                ssize_t need_size = buf_size + data->length - data->offset + 2;
                if (need_size > jpeg_buf_size)
                    jpeg_buf = realloc(jpeg_buf, jpeg_buf_size = need_size);
                memcpy(jpeg_buf + buf_size, data->data + data->offset,
                    data->length - data->offset);
                buf_size += data->length - data->offset;
            }
            if (app_config.jpeg_enable)
                send_jpeg_to_client(index, jpeg_buf, buf_size);
            break;
        }
        default:
            return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int media_start(void) {
    int ret = EXIT_SUCCESS;

    for (int i = 0; app_config.stream_dests[i] && *app_config.stream_dests[i]; i++) {
        if (STARTS_WITH(app_config.stream_dests[i], "rtmp://")) {
            flv_set_config(app_config.mp4_width, app_config.mp4_height, app_config.mp4_fps,
                app_config.audio_enable ? HAL_AUDCODEC_MP3 : HAL_AUDCODEC_UNSPEC,
                app_config.audio_bitrate, 1, app_config.audio_srate);
            rtmp_init(app_config.stream_dests[i]);
            continue;
        }

        if (STARTS_WITH(app_config.stream_dests[i], "udp://")) {
            char *endptr, *hostptr, *portptr, dst[16];
            unsigned short port = 0;
            long val;

            if (portptr = strrchr(app_config.stream_dests[i], ':')) {
                val = strtol(portptr + 1, &endptr, 10);
                if (endptr != portptr + 1)
                    port = (unsigned short)val;
                else {
                    if (portptr[2] != '/')
                        HAL_DANGER("media", "Invalid UDP port: %s, going with defaults!\n",
                            app_config.stream_dests[i]);
                }
            }

            hostptr = &app_config.stream_dests[i][6];
            if (portptr) {
                size_t hostlen = portptr - hostptr;
                if (hostlen > sizeof(dst) - 1) hostlen = sizeof(dst) - 1;
                strncpy(dst, hostptr, hostlen);
                dst[hostlen] = '\0';
            } else {
                strncpy(dst, hostptr, sizeof(dst) - 1);
                dst[sizeof(dst) - 1] = '\0';
            }

            /* "udp://host" without a port must not be sent to port 0: the
             * configured source port is also the default destination port. */
            if (!port)
                port = app_config.stream_udp_srcport ?
                    app_config.stream_udp_srcport : UDP_DEFAULT_PORT;

            if (!udpOn) {
                /* The multicast flag is the first octet of the host part:
                 * 224..239 is IPv4 multicast, everything else is unicast. */
                val = strtol(hostptr, &endptr, 10);
                const char *mcast =
                    (endptr != hostptr && val >= 224 && val <= 239) ? dst : NULL;

                /* udp_stream_init() returns EXIT_SUCCESS (0) on success. */
                if (udp_stream_init(app_config.stream_udp_srcport, mcast) != EXIT_SUCCESS) {
                    HAL_ERROR("media", "Failed to initialize UDP streaming for %s\n",
                        app_config.stream_dests[i]);
                    ret = EXIT_FAILURE;
                    continue;
                }
                udpOn = 1;
            }

            /* Every configured dest must be registered, not just the first
             * one that initializes the socket. */
            if (udp_stream_add_client(dst, port) != -1)
                HAL_INFO("media", "Starting streaming to %s...\n", app_config.stream_dests[i]);
            else {
                HAL_ERROR("media", "Failed to add UDP client %s:%u\n", dst, port);
                ret = EXIT_FAILURE;
            }
        }
    }

    return ret;
}

void media_stop(void) {
    if (udpOn) {
        udp_stream_close();
        udpOn = 0;
    }
    rtmp_close();
}

void request_idr(int index) {
    if (index < 0 || index >= chnCount) return;
    pthread_mutex_lock(&chnMtx);
    if (!chnState[index].enable) index = -1;
    if (index != -1) switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_I6:  i6_video_request_idr(index); break;
        case HAL_PLATFORM_I6C: i6c_video_request_idr(index); break;
        case HAL_PLATFORM_M6:  m6_video_request_idr(index); break;
        case HAL_PLATFORM_RK:  rk_video_request_idr(index); break;
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_AK:  ak_video_request_idr(index); break;
        case HAL_PLATFORM_GM:  gm_video_request_idr(index); break;
        case HAL_PLATFORM_V1:  v1_video_request_idr(index); break;
        case HAL_PLATFORM_V2:  v2_video_request_idr(index); break;
        case HAL_PLATFORM_V3:  v3_video_request_idr(index); break;
        case HAL_PLATFORM_V4:  v4_video_request_idr(index); break;
        #if __ARM_ARCH == 6
        case HAL_PLATFORM_F1:  f1_video_request_idr(index); break;
        case HAL_PLATFORM_F2:  f2_video_request_idr(index); break;
        #endif
#elif defined(__mips__)
        case HAL_PLATFORM_T31: t31_video_request_idr(index); break;
#elif defined(__riscv) || defined(__riscv__)
        case HAL_PLATFORM_CVI: cvi_video_request_idr(index); break;
#endif
    }
    pthread_mutex_unlock(&chnMtx);
}

void set_grayscale(bool active) {
    pthread_mutex_lock(&chnMtx);
    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_I6:  i6_channel_grayscale(active); break;
        case HAL_PLATFORM_I6C: i6c_channel_grayscale(active); break;
        case HAL_PLATFORM_M6:  m6_channel_grayscale(active); break;
        case HAL_PLATFORM_RK:  rk_channel_grayscale(active); break;
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_AK:  ak_channel_grayscale(active); break;
        case HAL_PLATFORM_V1:  v1_channel_grayscale(active); break;
        case HAL_PLATFORM_V2:  v2_channel_grayscale(active); break;
        case HAL_PLATFORM_V3:  v3_channel_grayscale(active); break;
        case HAL_PLATFORM_V4:  v4_channel_grayscale(active); break;
        #if __ARM_ARCH == 6
        case HAL_PLATFORM_F1:  f1_channel_grayscale(active); break;
        case HAL_PLATFORM_F2:  f2_channel_grayscale(active); break;
        #endif
#elif defined(__mips__)
        case HAL_PLATFORM_T31: t31_channel_grayscale(active); break;
#elif defined(__riscv) || defined(__riscv__)
        case HAL_PLATFORM_CVI: cvi_channel_grayscale(active); break;
#endif
    }
    pthread_mutex_unlock(&chnMtx);
}

int take_next_free_channel(bool mainLoop) {
    pthread_mutex_lock(&chnMtx);
    for (int i = 0; i < chnCount; i++) {
        /* The last VPSS channel belongs to IVS or to yolo.source=vpss3 and
         * must never be handed to a VENC stream at the same time. */
        if (i == HAL_CHN_IVS &&
            (app_config.ivs_enable ||
             (app_config.yolo_enable && app_config.yolo_source == 1)))
            continue;
        if (chnState[i].enable) continue;
        chnState[i].enable = true;
        chnState[i].mainLoop = mainLoop;
        chnState[i].role = HAL_CHNROLE_NONE;
        pthread_mutex_unlock(&chnMtx);
        return i;
    }
    pthread_mutex_unlock(&chnMtx);
    return -1;
}

int channel_by_role(int role) {
    int index = -1;

    pthread_mutex_lock(&chnMtx);
    for (int i = 0; i < chnCount; i++) {
        if (!chnState[i].enable) continue;
        if (chnState[i].role != role) continue;
        index = i;
        break;
    }
    pthread_mutex_unlock(&chnMtx);
    return index;
}

int create_channel(char index, short width, short height, char framerate, char jpeg) {
    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_I6:  return i6_channel_create(index, width, height, jpeg);
        case HAL_PLATFORM_I6C: return i6c_channel_create(index, width, height, jpeg);
        case HAL_PLATFORM_M6:  return m6_channel_create(index, width, height, jpeg);
        case HAL_PLATFORM_RK:  return rk_channel_create(index, width, height,
            app_config.mirror, app_config.flip);
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_AK:  return EXIT_SUCCESS;
        case HAL_PLATFORM_GM:  return EXIT_SUCCESS;
        case HAL_PLATFORM_V1:  return v1_channel_create(index, width, height,
            app_config.mirror, app_config.flip, framerate);
        case HAL_PLATFORM_V2:  return v2_channel_create(index, width, height,
            app_config.mirror, app_config.flip, framerate);
        case HAL_PLATFORM_V3:  return v3_channel_create(index, width, height,
            app_config.mirror, app_config.flip, framerate);
        case HAL_PLATFORM_V4:  return v4_channel_create(index, app_config.mirror,
            app_config.flip, framerate);
        #if __ARM_ARCH == 6
        case HAL_PLATFORM_F1:  return f1_channel_create(index, width, height, framerate, jpeg);
        case HAL_PLATFORM_F2:  return f2_channel_create(index, width, height, framerate, jpeg);
        #endif
#elif defined(__mips__)
        case HAL_PLATFORM_T31: return t31_channel_create(index, width, height,
            framerate, jpeg);
#elif defined(__riscv) || defined(__riscv__)
        case HAL_PLATFORM_CVI: return cvi_channel_create(index, width, height,
            framerate, app_config.mirror, app_config.flip);
#endif
    }
}

int bind_channel(char index, char framerate, char jpeg) {
    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_I6:  return i6_channel_bind(index, framerate);
        case HAL_PLATFORM_I6C: return i6c_channel_bind(index, framerate);
        case HAL_PLATFORM_M6:  return m6_channel_bind(index, framerate);
        case HAL_PLATFORM_RK:  return rk_channel_bind(index);
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_AK:  return ak_channel_bind(index);
        case HAL_PLATFORM_GM:  return gm_channel_bind(index);
        case HAL_PLATFORM_V1:  return v1_channel_bind(index);
        case HAL_PLATFORM_V2:  return v2_channel_bind(index);
        case HAL_PLATFORM_V3:  return v3_channel_bind(index);
        case HAL_PLATFORM_V4:  return v4_channel_bind(index);
        #if __ARM_ARCH == 6
        case HAL_PLATFORM_F1:  return f1_channel_bind(index);
        case HAL_PLATFORM_F2:  return f2_channel_bind(index);
        #endif
#elif defined(__mips__)
        case HAL_PLATFORM_T31: return t31_channel_bind(index);
#elif defined(__riscv) || defined(__riscv__)
        case HAL_PLATFORM_CVI: return cvi_channel_bind(index);
#endif
    }
}

int unbind_channel(char index, char jpeg) {
    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_I6:  return i6_channel_unbind(index);
        case HAL_PLATFORM_I6C: return i6c_channel_unbind(index);
        case HAL_PLATFORM_M6:  return m6_channel_unbind(index);
        case HAL_PLATFORM_RK:  return rk_channel_unbind(index);
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_AK:  return ak_channel_unbind(index);
        case HAL_PLATFORM_GM:  return gm_channel_unbind(index);
        case HAL_PLATFORM_V1:  return v1_channel_unbind(index);
        case HAL_PLATFORM_V2:  return v2_channel_unbind(index);
        case HAL_PLATFORM_V3:  return v3_channel_unbind(index);
        case HAL_PLATFORM_V4:  return v4_channel_unbind(index);
        #if __ARM_ARCH == 6
        case HAL_PLATFORM_F1:  return f1_channel_unbind(index);
        case HAL_PLATFORM_F2:  return f2_channel_unbind(index);
        #endif
#elif defined(__mips__)
        case HAL_PLATFORM_T31: return t31_channel_unbind(index);
#elif defined(__riscv) || defined(__riscv__)
        case HAL_PLATFORM_CVI: return cvi_channel_unbind(index);
#endif
    }
}

int media_video_disable(char index, char jpeg) {
    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_I6:  return i6_video_destroy(index);
        case HAL_PLATFORM_I6C: return i6c_video_destroy(index);
        case HAL_PLATFORM_M6:  return m6_video_destroy(index);
        case HAL_PLATFORM_RK:  return rk_video_destroy(index);
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_AK:  return ak_video_destroy(index);
        case HAL_PLATFORM_GM:  return gm_video_destroy(index);
        case HAL_PLATFORM_V1:  return v1_video_destroy(index);
        case HAL_PLATFORM_V2:  return v2_video_destroy(index);
        case HAL_PLATFORM_V3:  return v3_video_destroy(index);
        case HAL_PLATFORM_V4:  return v4_video_destroy(index);
        #if __ARM_ARCH == 6
        case HAL_PLATFORM_F1:  return f1_video_destroy(index);
        case HAL_PLATFORM_F2:  return f2_video_destroy(index);
        #endif
#elif defined(__mips__)
        case HAL_PLATFORM_T31: return t31_video_destroy(index);
#elif defined(__riscv) || defined(__riscv__)
        case HAL_PLATFORM_CVI: return cvi_video_destroy(index);
#endif
    }
    return 0;
}

void media_audio_disable(void) {
    if (!audioOn) return;

    audioOn = 0;

    pthread_join(aencPid, NULL);
    pthread_join(audPid, NULL);
    ringbuf_destroy(&audRing);
    shine_close(mp3Enc);

    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_I6:  i6_audio_deinit(); break;
        case HAL_PLATFORM_I6C: i6c_audio_deinit(); break;
        case HAL_PLATFORM_M6:  m6_audio_deinit(); break;
        case HAL_PLATFORM_RK:  rk_audio_deinit(); break;
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_AK:  ak_audio_deinit(); break;
        case HAL_PLATFORM_GM:  gm_audio_deinit(); break;
        case HAL_PLATFORM_V1:  v1_audio_deinit(); break;
        case HAL_PLATFORM_V2:  v2_audio_deinit(); break;
        case HAL_PLATFORM_V3:  v3_audio_deinit(); break;
        case HAL_PLATFORM_V4:  v4_audio_deinit(); break;
        #if __ARM_ARCH == 6
        case HAL_PLATFORM_F1:  f1_audio_deinit(); break;
        case HAL_PLATFORM_F2:  f2_audio_deinit(); break;
        #endif
#elif defined(__mips__)
        case HAL_PLATFORM_T31: t31_audio_deinit(); break;
#elif defined(__riscv) || defined(__riscv__)
        case HAL_PLATFORM_CVI: cvi_audio_deinit(); break;
#endif
    }
}

int media_audio_enable(void) {
    int ret = EXIT_SUCCESS;

    if (audioOn) return ret;

    if (ringbuf_init(&audRing, AUD_RING_CAP))
        HAL_ERROR("media", "Audio queue initialization failed!\n");

    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_I6:  ret = i6_audio_init(app_config.audio_srate, app_config.audio_gain); break;
        case HAL_PLATFORM_I6C: ret = i6c_audio_init(app_config.audio_srate, app_config.audio_gain); break;
        case HAL_PLATFORM_M6:  ret = m6_audio_init(app_config.audio_srate, app_config.audio_gain); break;
        case HAL_PLATFORM_RK:  ret = rk_audio_init(app_config.audio_srate); break;
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_AK:  ret = ak_audio_init(app_config.audio_srate); break;
        case HAL_PLATFORM_GM:  ret = gm_audio_init(app_config.audio_srate); break;
        case HAL_PLATFORM_V1:  ret = v1_audio_init(app_config.audio_srate); break;
        case HAL_PLATFORM_V2:  ret = v2_audio_init(app_config.audio_srate); break;
        case HAL_PLATFORM_V3:  ret = v3_audio_init(app_config.audio_srate); break;
        case HAL_PLATFORM_V4:  ret = v4_audio_init(app_config.audio_srate); break;
        #if __ARM_ARCH == 6
        case HAL_PLATFORM_F1:  ret = f1_audio_init(app_config.audio_srate); break;
        case HAL_PLATFORM_F2:  ret = f2_audio_init(app_config.audio_srate); break;
        #endif
#elif defined(__mips__)
        case HAL_PLATFORM_T31: ret = t31_audio_init(app_config.audio_srate); break;
#elif defined(__riscv) || defined(__riscv__)
        case HAL_PLATFORM_CVI: ret = cvi_audio_init(app_config.audio_srate); break;
#endif
    }
    if (ret)
        HAL_ERROR("media", "Audio initialization failed with %#x!\n%s\n",
            ret, errstr(ret));

    if (shine_check_config(app_config.audio_srate, app_config.audio_bitrate) < 0)
        HAL_ERROR("media", "MP3 samplerate/bitrate configuration is unsupported!\n");
    else {
        mp3Cnf.mpeg.mode = MONO;
        mp3Cnf.mpeg.bitr = app_config.audio_bitrate;
        mp3Cnf.mpeg.emph = NONE;
        mp3Cnf.mpeg.copyright = 0;
        mp3Cnf.mpeg.original = 1;
        mp3Cnf.wave.channels = PCM_MONO;
        mp3Cnf.wave.samplerate = app_config.audio_srate;
        if (!(mp3Enc = shine_initialise(&mp3Cnf)))
            HAL_ERROR("media", "MP3 encoder initialization failed!\n");

        pcmSamp = shine_samples_per_pass(mp3Enc);
    }

    {
        pthread_attr_t thread_attr;
        pthread_attr_init(&thread_attr);
        size_t stacksize;
        pthread_attr_getstacksize(&thread_attr, &stacksize);
        size_t new_stacksize = 16384;
        if (pthread_attr_setstacksize(&thread_attr, new_stacksize))
            HAL_DANGER("media", "Can't set stack size %zu\n", new_stacksize);
        if (pthread_create(
                        &audPid, &thread_attr, (void *(*)(void *))aud_thread, NULL))
            HAL_ERROR("media", "Starting the audio capture thread failed!\n");
        if (pthread_attr_setstacksize(&thread_attr, stacksize))
            HAL_DANGER("media", "Can't set stack size %zu\n", stacksize);
        pthread_attr_destroy(&thread_attr);
    }

    {
        pthread_attr_t thread_attr;
        pthread_attr_init(&thread_attr);
        size_t stacksize;
        pthread_attr_getstacksize(&thread_attr, &stacksize);
        size_t new_stacksize = 16384;
        if (pthread_attr_setstacksize(&thread_attr, new_stacksize))
            HAL_DANGER("media", "Can't set stack size %zu\n", new_stacksize);
        if (pthread_create(
                        &aencPid, &thread_attr, (void *(*)(void *))aenc_thread, NULL))
            HAL_ERROR("media", "Starting the audio encoding thread failed!\n");
        if (pthread_attr_setstacksize(&thread_attr, stacksize))
            HAL_DANGER("media", "Can't set stack size %zu\n", stacksize);
        pthread_attr_destroy(&thread_attr);
    }

    audioOn = 1;

    return ret;
}

int media_mjpeg_disable(void) {
    int ret;

    for (char i = 0; i < chnCount; i++) {
        if (!chnState[i].enable) continue;
        if (chnState[i].payload != HAL_VIDCODEC_MJPG) continue;

        if (ret = unbind_channel(i, 1))
            HAL_ERROR("media", "Unbinding channel %d failed with %#x!\n%s\n",
                i, ret, errstr(ret));

        if (ret = media_video_disable(i, 1))
            HAL_ERROR("media", "Disabling encoder %d failed with %#x!\n%s\n",
                i, ret, errstr(ret));
    }

    return EXIT_SUCCESS;
}

int media_mjpeg_enable(void) {
    int ret;

    int index = take_next_free_channel(true);
    if (index < 0) {
        HAL_ERROR("media", "No free channel for MJPEG!\n");
    }
    chnState[index].role = HAL_CHNROLE_MJPG;

    if (ret = create_channel(index, app_config.mjpeg_width,
        app_config.mjpeg_height, app_config.mjpeg_fps, 1))
    {
        /* Only the slot needs releasing. */
        chnState[index].enable = false;
        chnState[index].role = HAL_CHNROLE_NONE;
        HAL_ERROR("media", "Creating channel %d failed with %#x!\n%s\n",
            index, ret, errstr(ret));
    }

    {
        hal_vidconfig config;
        config.width = app_config.mjpeg_width;
        config.height = app_config.mjpeg_height;
        config.codec = HAL_VIDCODEC_MJPG;
        config.mode = app_config.mjpeg_mode;
        config.framerate = app_config.mjpeg_fps;
        config.bitrate = app_config.mjpeg_bitrate;
        config.maxBitrate = app_config.mjpeg_bitrate * 5 / 4;

        switch (plat) {
#if defined(__ARM_PCS_VFP)
            case HAL_PLATFORM_I6:  ret = i6_video_create(index, &config); break;
            case HAL_PLATFORM_I6C: ret = i6c_video_create(index, &config); break;
            case HAL_PLATFORM_M6:  ret = m6_video_create(index, &config); break;
            case HAL_PLATFORM_RK:  ret = rk_video_create(index, &config); break;
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
            case HAL_PLATFORM_AK:  ret = ak_video_create(index, &config); break;
            case HAL_PLATFORM_GM:  ret = gm_video_create(index, &config); break;
            case HAL_PLATFORM_V1:  ret = v1_video_create(index, &config); break;
            case HAL_PLATFORM_V2:  ret = v2_video_create(index, &config); break;
            case HAL_PLATFORM_V3:  ret = v3_video_create(index, &config); break;
            case HAL_PLATFORM_V4:  ret = v4_video_create(index, &config); break;
            #if __ARM_ARCH == 6
            case HAL_PLATFORM_F1:  ret = f1_video_create(index, &config); break;
            case HAL_PLATFORM_F2:  ret = f2_video_create(index, &config); break;
            #endif
#elif defined(__mips__)
            case HAL_PLATFORM_T31: ret = t31_video_create(index, &config); break;
#elif defined(__riscv) || defined(__riscv__)
            case HAL_PLATFORM_CVI: ret = cvi_video_create(index, &config); break;
#endif
        }

        if (ret)
        {
            /* Release the channel before clearing the slot. */
            media_video_disable(index, 1);
            chnState[index].enable = false;
            chnState[index].role = HAL_CHNROLE_NONE;
            HAL_ERROR("media", "Creating encoder %d failed with %#x!\n%s\n",
                index, ret, errstr(ret));
        }
    }

    if (ret = bind_channel(index, app_config.mjpeg_fps, 1))
    {
        /* Drop the encoder before releasing the slot. */
        media_video_disable(index, 1);
        chnState[index].enable = false;
        chnState[index].role = HAL_CHNROLE_NONE;
        HAL_ERROR("media", "Binding channel %d failed with %#x!\n%s\n",
            index, ret, errstr(ret));
    }

    return EXIT_SUCCESS;
}

int media_mp4_disable(void) {
    int ret;

    for (char i = 0; i < chnCount; i++) {
        if (!chnState[i].enable) continue;
        if (chnState[i].role != HAL_CHNROLE_MAIN &&
            chnState[i].role != HAL_CHNROLE_NONE) continue;
        if (chnState[i].payload != HAL_VIDCODEC_H264 &&
            chnState[i].payload != HAL_VIDCODEC_H265) continue;

        if (ret = unbind_channel(i, 1))
            HAL_ERROR("media", "Unbinding channel %d failed with %#x!\n%s\n",
                i, ret, errstr(ret));

        if (ret = media_video_disable(i, 1))
            HAL_ERROR("media", "Disabling encoder %d failed with %#x!\n%s\n",
                i, ret, errstr(ret));
        chnState[i].enable = false;
        chnState[i].role = HAL_CHNROLE_NONE;
    }

    return EXIT_SUCCESS;
}

int media_mp4_enable(void) {
    int ret;

    int index = take_next_free_channel(true);
    if (index < 0) {
        HAL_ERROR("media", "No free channel for the main stream!\n");
    }
    chnState[index].role = HAL_CHNROLE_MAIN;

    if (ret = create_channel(index, app_config.mp4_width,
        app_config.mp4_height, app_config.mp4_fps, 0))
    {
        /* Only the slot needs releasing. */
        chnState[index].enable = false;
        chnState[index].role = HAL_CHNROLE_NONE;
        HAL_ERROR("media", "Creating channel %d failed with %#x!\n%s\n",
            index, ret, errstr(ret));
    }

    {
        hal_vidconfig config;
        memset(&config, 0, sizeof(config));
        config.width = app_config.mp4_width;
        config.height = app_config.mp4_height;
        config.codec = app_config.mp4_codecH265 ?
            HAL_VIDCODEC_H265 : HAL_VIDCODEC_H264;
        config.mode = app_config.mp4_mode;
        config.profile = app_config.mp4_profile;
        config.gop = app_config.mp4_gop;
        config.framerate = app_config.mp4_fps;
        config.bitrate = app_config.mp4_bitrate;
        config.maxBitrate = app_config.mp4_bitrate * 5 / 4;
        config.smartp = app_config.venc_smartp;
        config.virIdr = app_config.venc_vir_idr;
        config.svc = app_config.venc_svc;
        config.motionDeblur = app_config.venc_motion_deblur;
        config.minQp = app_config.venc_min_qp;
        config.maxQp = app_config.venc_max_qp;
        config.minIQp = app_config.venc_min_iqp;
        config.maxIQp = app_config.venc_max_iqp;

        switch (plat) {
#if defined(__ARM_PCS_VFP)
            case HAL_PLATFORM_I6:  ret = i6_video_create(index, &config); break;
            case HAL_PLATFORM_I6C: ret = i6c_video_create(index, &config); break;
            case HAL_PLATFORM_M6:  ret = m6_video_create(index, &config); break;
            case HAL_PLATFORM_RK:  ret = rk_video_create(index, &config); break;
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
            case HAL_PLATFORM_AK:  ret = ak_video_create(index, &config); break;
            case HAL_PLATFORM_GM:  ret = gm_video_create(index, &config); break;
            case HAL_PLATFORM_V1:  ret = v1_video_create(index, &config); break;
            case HAL_PLATFORM_V2:  ret = v2_video_create(index, &config); break;
            case HAL_PLATFORM_V3:  ret = v3_video_create(index, &config); break;
            case HAL_PLATFORM_V4:  ret = v4_video_create(index, &config); break;
            #if __ARM_ARCH == 6
            case HAL_PLATFORM_F1:  ret = f1_video_create(index, &config); break;
            case HAL_PLATFORM_F2:  ret = f2_video_create(index, &config); break;
            #endif
#elif defined(__mips__)
            case HAL_PLATFORM_T31: ret = t31_video_create(index, &config); break;
#elif defined(__riscv) || defined(__riscv__)
            case HAL_PLATFORM_CVI: ret = cvi_video_create(index, &config); break;
#endif
        }

        if (ret)
        {
            /* Release the channel before clearing the slot. */
            media_video_disable(index, 0);
            chnState[index].enable = false;
            chnState[index].role = HAL_CHNROLE_NONE;
            HAL_ERROR("media", "Creating encoder %d failed with %#x!\n%s\n",
                index, ret, errstr(ret));
        }

        mp4_set_config(app_config.mp4_width, app_config.mp4_height, app_config.mp4_fps,
            app_config.audio_enable ? HAL_AUDCODEC_MP3 : HAL_AUDCODEC_UNSPEC,
            app_config.audio_bitrate, 1, app_config.audio_srate);
    }

    if (ret = bind_channel(index, app_config.mp4_fps, 0))
    {
        /* Drop the encoder before releasing the slot. */
        media_video_disable(index, 0);
        chnState[index].enable = false;
        chnState[index].role = HAL_CHNROLE_NONE;
        HAL_ERROR("media", "Binding channel %d failed with %#x!\n%s\n",
            index, ret, errstr(ret));
    }

#if defined(__ARM_PCS_VFP)
    if (plat == HAL_PLATFORM_RK && rtspHandle)
        rtsp_set_stream_channel(rtspHandle, 0, index);
#endif
    media_eptz_mark_applied(index);
    media_rotate_mark_applied(index);
    return EXIT_SUCCESS;
}

int media_substream_enable(void) {
    int ret;
    int index = channel_by_role(HAL_CHNROLE_SUB);

    /* The channel may still exist after a soft stop; only create it once. */
    if (index >= 0) {
#if defined(__ARM_PCS_VFP)
        if (plat == HAL_PLATFORM_RK && rtspHandle)
            rtsp_set_stream_channel(rtspHandle, 1, index);
#endif
        HAL_INFO("media", "Sub stream resumed on channel %d\n", index);
        return EXIT_SUCCESS;
    }

    index = take_next_free_channel(true);

    if (index < 0) {
        HAL_ERROR("media", "No free channel for the sub stream!\n");
    }
    chnState[index].role = HAL_CHNROLE_SUB;

    if (ret = create_channel(index, app_config.substream_width,
        app_config.substream_height, app_config.substream_fps, 0))
    {
        chnState[index].enable = false;
        chnState[index].role = HAL_CHNROLE_NONE;
        HAL_ERROR("media", "Creating sub channel %d failed with %#x!\n%s\n",
            index, ret, errstr(ret));
    }

    {
        hal_vidconfig config;
        memset(&config, 0, sizeof(config));
        config.width = app_config.substream_width;
        config.height = app_config.substream_height;
        config.codec = app_config.substream_codecH265 ?
            HAL_VIDCODEC_H265 : HAL_VIDCODEC_H264;
        config.mode = app_config.substream_mode;
        config.profile = app_config.substream_profile;
        config.gop = app_config.substream_gop;
        config.framerate = app_config.substream_fps;
        config.bitrate = app_config.substream_bitrate;
        config.maxBitrate = app_config.substream_bitrate * 5 / 4;
        config.smartp = app_config.venc_smartp;
        config.virIdr = app_config.venc_vir_idr;
        config.svc = app_config.venc_svc;
        config.motionDeblur = app_config.venc_motion_deblur;
        config.minQp = app_config.venc_min_qp;
        config.maxQp = app_config.venc_max_qp;
        config.minIQp = app_config.venc_min_iqp;
        config.maxIQp = app_config.venc_max_iqp;

        switch (plat) {
#if defined(__ARM_PCS_VFP)
            case HAL_PLATFORM_I6:  ret = i6_video_create(index, &config); break;
            case HAL_PLATFORM_I6C: ret = i6c_video_create(index, &config); break;
            case HAL_PLATFORM_M6:  ret = m6_video_create(index, &config); break;
            case HAL_PLATFORM_RK:  ret = rk_video_create(index, &config); break;
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
            case HAL_PLATFORM_AK:  ret = ak_video_create(index, &config); break;
            case HAL_PLATFORM_GM:  ret = gm_video_create(index, &config); break;
            case HAL_PLATFORM_V1:  ret = v1_video_create(index, &config); break;
            case HAL_PLATFORM_V2:  ret = v2_video_create(index, &config); break;
            case HAL_PLATFORM_V3:  ret = v3_video_create(index, &config); break;
            case HAL_PLATFORM_V4:  ret = v4_video_create(index, &config); break;
            #if __ARM_ARCH == 6
            case HAL_PLATFORM_F1:  ret = f1_video_create(index, &config); break;
            case HAL_PLATFORM_F2:  ret = f2_video_create(index, &config); break;
            #endif
#elif defined(__mips__)
            case HAL_PLATFORM_T31: ret = t31_video_create(index, &config); break;
#elif defined(__riscv) || defined(__riscv__)
            case HAL_PLATFORM_CVI: ret = cvi_video_create(index, &config); break;
#endif
            default: ret = EXIT_FAILURE; break;
        }

        if (ret)
        {
            if (plat == HAL_PLATFORM_RK) media_video_disable(index, 0);
            chnState[index].enable = false;
            chnState[index].role = HAL_CHNROLE_NONE;
            HAL_ERROR("media", "Creating sub encoder %d failed with %#x!\n%s\n",
                index, ret, errstr(ret));
        }
    }

    if (ret = bind_channel(index, app_config.substream_fps, 0))
    {
        if (plat == HAL_PLATFORM_RK) media_video_disable(index, 0);
        chnState[index].enable = false;
        chnState[index].role = HAL_CHNROLE_NONE;
        HAL_ERROR("media", "Binding sub channel %d failed with %#x!\n%s\n",
            index, ret, errstr(ret));
    }

#if defined(__ARM_PCS_VFP)
    if (plat == HAL_PLATFORM_RK && rtspHandle)
        rtsp_set_stream_channel(rtspHandle, 1, index);
#endif
    HAL_INFO("media", "Sub stream channel %d at %dx%d@%d\n",
        index, app_config.substream_width, app_config.substream_height,
        app_config.substream_fps);
    media_eptz_mark_applied(index);
    media_rotate_mark_applied(index);
    return EXIT_SUCCESS;
}

int media_substream_disable(void) {
    /* Stop serving the sub stream but keep the VENC/VPSS channel allocated.
     * On RV1106 destroying a channel while RGN overlays are attached to the
     * main VENC invalidates those regions (RK_MPI_RGN_SetBitMap starts
     * returning 0xfffffffe); the channel is released by sdk_stop(). */
    int index = channel_by_role(HAL_CHNROLE_SUB);

    if (index < 0) return EXIT_SUCCESS;

#if defined(__ARM_PCS_VFP)
    if (plat == HAL_PLATFORM_RK && rtspHandle) {
        rtsp_set_stream_channel(rtspHandle, 1, -1);
        rtsp_disable_stream(rtspHandle, 1);
    }
#endif
    HAL_INFO("media", "Sub stream channel %d stopped (kept allocated)\n", index);
    return EXIT_SUCCESS;
}

static pthread_mutex_t eptzStateMutex = PTHREAD_MUTEX_INITIALIZER;
static int eptzLastChn = -1;
static int eptzLastRc;
/* What rk_channel_bind() actually applied on the last channel start. */
static int eptzLiveValid, eptzLiveEnable, eptzLiveStream;
static int eptzLiveX, eptzLiveY, eptzLiveW, eptzLiveH;
static int rotateLiveValid, rotateLiveEnable, rotateLiveStream, rotateLiveAngle;
static int mediaStarting;

/* Must be called with eptzStateMutex held: the channel was just built by the
 * bind hook, so the live state equals the stored configuration. */
static void eptz_live_store_locked(int index) {
    eptzLastChn = index;
    eptzLastRc = 0;
    eptzLiveValid = 1;
    eptzLiveEnable = app_config.eptz_enable;
    eptzLiveStream = app_config.eptz_stream;
    if (app_config.eptz_enable) {
        eptzLiveX = app_config.eptz_x;
        eptzLiveY = app_config.eptz_y;
        eptzLiveW = app_config.eptz_w;
        eptzLiveH = app_config.eptz_h;
    } else {
        /* A disabled crop is a full-frame channel, not the stored rect. */
        eptzLiveX = 0;
        eptzLiveY = 0;
        eptzLiveW = app_config.mp4_width ? app_config.mp4_width : 1920;
        eptzLiveH = app_config.mp4_height ? app_config.mp4_height : 1080;
    }
}

static void rotate_live_store_locked(void) {
    rotateLiveValid = 1;
    rotateLiveEnable = app_config.rotate_enable;
    rotateLiveStream = app_config.rotate_stream;
    rotateLiveAngle = app_config.rotate_enable ? app_config.rotate_angle : 0;
}

/* Records the stored crop as applied by rk_channel_bind() on a channel start.
 * Called after a successful bind of the channel the crop belongs to. */
static void media_eptz_mark_applied(char bound) {
    int index = channel_by_role(app_config.eptz_stream ? HAL_CHNROLE_SUB : HAL_CHNROLE_MAIN);

    if (index < 0 || index != bound) return;

    pthread_mutex_lock(&eptzStateMutex);
    eptz_live_store_locked(index);
    pthread_mutex_unlock(&eptzStateMutex);
}

/* Same bookkeeping for the VENC rotation, which rk_channel_bind() applies
 * together with the crop. */
static void media_rotate_mark_applied(char bound) {
    int index = channel_by_role(app_config.rotate_stream ? HAL_CHNROLE_SUB : HAL_CHNROLE_MAIN);

    if (index < 0 || index != bound) return;

    pthread_mutex_lock(&eptzStateMutex);
    rotate_live_store_locked();
    pthread_mutex_unlock(&eptzStateMutex);
}

/* The RV1106 resets the crop/rotation when a channel is enabled, and
 * rk_channel_bind() re-applies the stored settings right after EnableChn.  A
 * runtime change is only stored here; the caller restarts the application so
 * the next channel start applies it, without touching a live stream. */
int media_apply_eptz(void) {
    int index;
    int same;

    index = channel_by_role(app_config.eptz_stream ? HAL_CHNROLE_SUB : HAL_CHNROLE_MAIN);

    pthread_mutex_lock(&eptzStateMutex);
    if (mediaStarting && index >= 0) {
        /* The bind hook has just applied the stored crop. */
        eptz_live_store_locked(index);
        pthread_mutex_unlock(&eptzStateMutex);
        return MEDIA_APPLY_OK;
    }
    same = eptzLiveValid &&
        eptzLiveEnable == app_config.eptz_enable &&
        eptzLiveStream == app_config.eptz_stream &&
        (!app_config.eptz_enable ||
            (eptzLiveX == app_config.eptz_x && eptzLiveY == app_config.eptz_y &&
             eptzLiveW == app_config.eptz_w && eptzLiveH == app_config.eptz_h));
    pthread_mutex_unlock(&eptzStateMutex);

    if (index < 0) return MEDIA_APPLY_NO_TARGET;
    if (!same) return MEDIA_APPLY_NEEDS_RESTART;
    return MEDIA_APPLY_OK;
}

/* Records the MPP code of the SetChnCrop call made by rk_channel_bind(), so
 * /api/eptz can report the last HAL result (0 = accepted). */
void media_eptz_bind_result(int rc) {
    pthread_mutex_lock(&eptzStateMutex);
    eptzLastRc = rc;
    pthread_mutex_unlock(&eptzStateMutex);
}

void media_eptz_last(int *chn, int *x, int *y, int *w, int *h, int *rc) {
    pthread_mutex_lock(&eptzStateMutex);
    if (chn) *chn = eptzLiveValid ? eptzLastChn : -1;
    if (x) *x = eptzLiveValid ? eptzLiveX : 0;
    if (y) *y = eptzLiveValid ? eptzLiveY : 0;
    if (w) *w = eptzLiveValid ? eptzLiveW : 0;
    if (h) *h = eptzLiveValid ? eptzLiveH : 0;
    if (rc) *rc = eptzLastRc;
    pthread_mutex_unlock(&eptzStateMutex);
}

/* A channel restart (MP4/sub/MJPEG enable) recreates the VPSS/VENC channel.
 * rk_channel_bind() re-applies the stored crop and rotation after EnableChn,
 * because the enable resets both. */

int media_apply_rotation(void) {
    int index;
    int reqAngle = app_config.rotate_enable ? app_config.rotate_angle : 0;
    int same;

    index = channel_by_role(app_config.rotate_stream ? HAL_CHNROLE_SUB : HAL_CHNROLE_MAIN);

    pthread_mutex_lock(&eptzStateMutex);
    if (mediaStarting && index >= 0) {
        rotate_live_store_locked();
        pthread_mutex_unlock(&eptzStateMutex);
        return MEDIA_APPLY_OK;
    }
    same = rotateLiveValid &&
        rotateLiveEnable == app_config.rotate_enable &&
        rotateLiveStream == app_config.rotate_stream &&
        rotateLiveAngle == reqAngle;
    pthread_mutex_unlock(&eptzStateMutex);

    if (index < 0) return MEDIA_APPLY_NO_TARGET;
    if (!same) return MEDIA_APPLY_NEEDS_RESTART;
    return MEDIA_APPLY_OK;
}

void media_rotate_last(int *enable, int *stream, int *angle) {
    pthread_mutex_lock(&eptzStateMutex);
    if (enable) *enable = rotateLiveValid ? rotateLiveEnable : -1;
    if (stream) *stream = rotateLiveValid ? rotateLiveStream : -1;
    if (angle) *angle = rotateLiveValid ? rotateLiveAngle : -1;
    pthread_mutex_unlock(&eptzStateMutex);
}

int media_apply_venc_extras(void) {
    hal_vidconfig config;
    int index, ret = EXIT_SUCCESS;

    memset(&config, 0, sizeof(config));
    config.svc = app_config.venc_svc;
    config.motionDeblur = app_config.venc_motion_deblur;
    config.minQp = app_config.venc_min_qp;
    config.maxQp = app_config.venc_max_qp;
    config.minIQp = app_config.venc_min_iqp;
    config.maxIQp = app_config.venc_max_iqp;

    index = channel_by_role(HAL_CHNROLE_MAIN);
    if (index >= 0) switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_RK:
            if (rk_video_apply_extras(index, &config)) ret = EXIT_FAILURE;
            break;
#endif
    }
    index = channel_by_role(HAL_CHNROLE_SUB);
    if (index >= 0) switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_RK:
            if (rk_video_apply_extras(index, &config)) ret = EXIT_FAILURE;
            break;
#endif
    }

    return ret;
}

int media_apply_aiisp(void) {
    hal_aiisp_config config;

    /* mode == OFF is a valid request: it calls SetGrpAIISPAttr(enable=0) so a
     * running model is actually released. */
    memset(&config, 0, sizeof(config));
    config.mode = (hal_aiisp_mode)app_config.aiisp_enable;
    snprintf(config.model, sizeof(config.model),
        "%s/", app_config.aiisp_model_dir);
    config.bufCnt = app_config.aiisp_buf_cnt;
    config.fpsEnabled = app_config.aiisp_fps_enabled;
    config.fpsDisabled = app_config.aiisp_fps_disabled;

    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_RK: return rk_group_set_aiisp(&config);
#endif
    }
    return EXIT_FAILURE;
}

int media_apply_iq(const char *main, const char *sub, const char *file) {
    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_RK:
            if (file && *file && rk_aiq_update_iq(file)) return EXIT_FAILURE;
            if (main && *main && rk_aiq_switch_scene(main, sub && *sub ? sub : "day"))
                return EXIT_FAILURE;
            return EXIT_SUCCESS;
#endif
    }
    return EXIT_FAILURE;
}

int sdk_start(void) {
    int ret;

    mediaStarting = 1;

    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_I3:  ret = i3_hal_init(); break;
        case HAL_PLATFORM_I6:  ret = i6_hal_init(); break;
        case HAL_PLATFORM_I6C: ret = i6c_hal_init(); break;
        case HAL_PLATFORM_M6:  ret = m6_hal_init(); break;
        case HAL_PLATFORM_RK:  ret = rk_hal_init(); break;
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_AK:  ret = ak_hal_init(); break;
        case HAL_PLATFORM_GM:  ret = gm_hal_init(); break;
        case HAL_PLATFORM_V1:  ret = v1_hal_init(); break;
        case HAL_PLATFORM_V2:  ret = v2_hal_init(); break;
        case HAL_PLATFORM_V3:  ret = v3_hal_init(); break;
        case HAL_PLATFORM_V4:  ret = v4_hal_init(); break;
        #if __ARM_ARCH == 6
        case HAL_PLATFORM_F1:  ret = f1_hal_init(); break;
        case HAL_PLATFORM_F2:  ret = f2_hal_init(); break;
        #endif
#elif defined(__mips__)
        case HAL_PLATFORM_T31: ret = t31_hal_init(); break;
#elif defined(__riscv) || defined(__riscv__)
        case HAL_PLATFORM_CVI: ret = cvi_hal_init(); break;
#endif
    }
    if (ret)
        HAL_ERROR("media", "HAL initialization failed with %#x!\n%s\n",
            ret, errstr(ret));

    /* osd.scope=all needs the multi-attach PoC to have passed (G4).  Until
     * then the safe single-channel behaviour is enforced here as well, so a
     * hand-edited YAML cannot enable an unverified path. */
    if (app_config.osd_scope_all && !(halCapabilities & HAL_CAP_OSD_MULTI)) {
        HAL_WARNING("media", "osd.scope=all is not verified on this build; "
            "using scope=main\n");
        app_config.osd_scope_all = false;
    }

    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_I6:
            i6_aud_cb = save_audio_stream;
            i6_vid_cb = save_video_stream;
            break;
        case HAL_PLATFORM_I6C:
            i6c_aud_cb = save_audio_stream;
            i6c_vid_cb = save_video_stream;
            break;
        case HAL_PLATFORM_M6:
            m6_aud_cb = save_audio_stream;
            m6_vid_cb = save_video_stream;
            break;
        case HAL_PLATFORM_RK:
            rk_aud_cb = save_audio_stream;
            rk_vid_cb = save_video_stream;
            break;
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_AK:
            ak_aud_cb = save_audio_stream;
            ak_vid_cb = save_video_stream;
            break;
        case HAL_PLATFORM_GM:
            gm_aud_cb = save_audio_stream;
            gm_vid_cb = save_video_stream;
            break;
        case HAL_PLATFORM_V1:
            v1_aud_cb = save_audio_stream;
            v1_vid_cb = save_video_stream;
            break;
        case HAL_PLATFORM_V2:
            v2_aud_cb = save_audio_stream;
            v2_vid_cb = save_video_stream;
            break;
        case HAL_PLATFORM_V3:
            v3_aud_cb = save_audio_stream;
            v3_vid_cb = save_video_stream;
            break;
        case HAL_PLATFORM_V4:
            v4_aud_cb = save_audio_stream;
            v4_vid_cb = save_video_stream;
            break;
        #if __ARM_ARCH == 6
        case HAL_PLATFORM_F1:
            f1_aud_cb = save_audio_stream;
            f1_vid_cb = save_video_stream;
            break;
        case HAL_PLATFORM_F2:
            f2_aud_cb = save_audio_stream;
            f2_vid_cb = save_video_stream;
            break;
        #endif
#elif defined(__mips__)
        case HAL_PLATFORM_T31:
            t31_aud_cb = save_audio_stream;
            t31_vid_cb = save_video_stream;
            break;
#elif defined(__riscv) || defined(__riscv__)
        case HAL_PLATFORM_CVI:
            cvi_aud_cb = save_audio_stream;
            cvi_vid_cb = save_video_stream;
            break;
#endif
    }

    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_I3:  ret = i3_system_init(); break;
        case HAL_PLATFORM_I6:  ret = i6_system_init(); break;
        case HAL_PLATFORM_I6C: ret = i6c_system_init(); break;
        case HAL_PLATFORM_M6:  ret = m6_system_init(); break;
        case HAL_PLATFORM_RK:  ret = rk_system_init(0); break;
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_AK:  ret = ak_system_init(app_config.sensor_config); break;
        case HAL_PLATFORM_GM:  ret = gm_system_init(); break;
        case HAL_PLATFORM_V1:  ret = v1_system_init(app_config.sensor_config); break;
        case HAL_PLATFORM_V2:  ret = v2_system_init(app_config.sensor_config); break;
        case HAL_PLATFORM_V3:  ret = v3_system_init(app_config.sensor_config); break;
        case HAL_PLATFORM_V4:  ret = v4_system_init(app_config.sensor_config); break;
        #if __ARM_ARCH == 6
        case HAL_PLATFORM_F1:  ret = f1_system_init(); break;
        case HAL_PLATFORM_F2:  ret = f2_system_init(); break;
        #endif
#elif defined(__mips__)
        case HAL_PLATFORM_T31: ret = t31_system_init(); break;
#elif defined(__riscv) || defined(__riscv__)
        case HAL_PLATFORM_CVI: ret = cvi_system_init(app_config.sensor_config); break;
#endif
    }
    if (ret)
        HAL_ERROR("media", "System initialization failed with %#x!\n%s\n",
            ret, errstr(ret));

    if (app_config.audio_enable) {
        ret = media_audio_enable();
        if (ret)
            HAL_ERROR("media", "Audio initialization failed with %#x!\n%s\n",
                ret, errstr(ret));
    }

    short width = MAX(app_config.mp4_width, app_config.mjpeg_width);
    short height = MAX(app_config.mp4_height, app_config.mjpeg_height);
    short framerate = MAX(app_config.mp4_fps, app_config.mjpeg_fps);

    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_I6:  ret = i6_pipeline_create(0, width,
            height, app_config.mirror, app_config.flip, framerate); break;
        case HAL_PLATFORM_I6C: ret = i6c_pipeline_create(0, width,
            height, app_config.mirror, app_config.flip, framerate); break;
        case HAL_PLATFORM_M6:  ret = m6_pipeline_create(0, width,
            height, app_config.mirror, app_config.flip, framerate); break;
        case HAL_PLATFORM_RK:  ret = rk_pipeline_create(width, height); break;
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_AK:  ret = ak_pipeline_create(width, height,
            app_config.mirror, app_config.flip); break;
        case HAL_PLATFORM_GM:  ret = gm_pipeline_create(app_config.mirror,
            app_config.flip); break;
        case HAL_PLATFORM_V1:  ret = v1_pipeline_create(); break;
        case HAL_PLATFORM_V2:  ret = v2_pipeline_create(); break;
        case HAL_PLATFORM_V3:  ret = v3_pipeline_create(); break;
        case HAL_PLATFORM_V4:  ret = v4_pipeline_create(); break;
        #if __ARM_ARCH == 6
        case HAL_PLATFORM_F1:  ret = f1_pipeline_create(width, height, app_config.mirror, app_config.flip, framerate, app_config.antiflicker); break;
        case HAL_PLATFORM_F2:  ret = f2_pipeline_create(width, height, app_config.mirror, app_config.flip, framerate, app_config.antiflicker); break;
        #endif
#elif defined(__mips__)
        case HAL_PLATFORM_T31: ret = t31_pipeline_create(app_config.mirror,
            app_config.flip, app_config.antiflicker, framerate); break;
#elif defined(__riscv) || defined(__riscv__)
        case HAL_PLATFORM_CVI: ret = cvi_pipeline_create(); break;
#endif
    }
    if (ret)
        HAL_ERROR("media", "Pipeline creation failed with %#x!\n%s\n",
            ret, errstr(ret));

#if defined(__arm__) && !defined(__ARM_PCS_VFP)
    if (plat == HAL_PLATFORM_AK && !ret)
        ak_pipeline_streamon();
#endif

    if (isp_thread) {
        pthread_attr_t thread_attr;
        pthread_attr_init(&thread_attr);
        size_t stacksize;
        pthread_attr_getstacksize(&thread_attr, &stacksize);
        size_t new_stacksize = app_config.isp_thread_stack_size;
        if (pthread_attr_setstacksize(&thread_attr, new_stacksize))
            HAL_DANGER("media", "Can't set stack size %zu!\n", new_stacksize);
        if (pthread_create(
                     &ispPid, &thread_attr, (void *(*)(void *))isp_thread, NULL))
            HAL_ERROR("media", "Starting the imaging thread failed!\n");
        if (pthread_attr_setstacksize(&thread_attr, stacksize))
            HAL_DANGER("media", "Can't set stack size %zu!\n", stacksize);
        pthread_attr_destroy(&thread_attr);
    }

    if (app_config.mp4_enable && (ret = media_mp4_enable()))
        HAL_ERROR("media", "MP4 initialization failed with %#x!\n", ret);

    if (app_config.substream_enable && (ret = media_substream_enable()))
        HAL_WARNING("media", "Sub stream initialization failed with %#x!\n", ret);

    if (app_config.mjpeg_enable && (ret = media_mjpeg_enable()))
        HAL_ERROR("media", "MJPEG initialization failed with %#x!\n", ret);

    if (app_config.jpeg_enable && (ret = jpeg_init()))
        HAL_ERROR("media", "JPEG initialization failed with %#x!\n", ret);

    /* Group/channel features are applied once every VENC channel exists. */
    if (app_config.aiisp_enable != HAL_AIISP_OFF) {
        /* Do not let a missing model or an unsupported library spam
         * rkPostIspInit failures on every frame. */
        gating_result gate = gating_check("aiisp");
        if (gate.ok) {
            if (media_apply_aiisp())
                HAL_WARNING("media", "AIISP could not be enabled\n");
        } else {
            HAL_WARNING("media", "AIISP disabled: %s\n", gate.reason);
            app_config.aiisp_enable = HAL_AIISP_OFF;
        }
    }
    if ((*app_config.iq_main_scene || *app_config.iq_file) &&
        media_apply_iq(app_config.iq_main_scene, app_config.iq_sub_scene,
            app_config.iq_file))
        HAL_WARNING("media", "IQ switch could not be applied\n");

    /* Record the crop/rotation that rk_channel_bind() applied for /api/eptz
     * without restarting a channel that has just been started.  With
     * mediaStarting set both calls only refresh the live snapshot. */
    (void)media_apply_eptz();
    (void)media_apply_rotation();
    mediaStarting = 0;

#if defined(__arm__) && !defined(__ARM_PCS_VFP)
    if (plat == HAL_PLATFORM_AK)
        ak_pipeline_streamon_sub();
#endif

    {
        pthread_attr_t thread_attr;
        pthread_attr_init(&thread_attr);
        size_t stacksize;
        pthread_attr_getstacksize(&thread_attr, &stacksize);
        size_t new_stacksize = app_config.venc_stream_thread_stack_size;
        if (pthread_attr_setstacksize(&thread_attr, new_stacksize))
            HAL_DANGER("media", "Can't set stack size %zu\n", new_stacksize);
        if (pthread_create(
                     &vidPid, &thread_attr, (void *(*)(void *))vid_thread, NULL))
            HAL_ERROR("media", "Starting the video encoding thread failed!\n");
        if (pthread_attr_setstacksize(&thread_attr, stacksize))
            HAL_DANGER("media", "Can't set stack size %zu\n", stacksize);
        pthread_attr_destroy(&thread_attr);
    }

    if (!access(app_config.sensor_config, F_OK) && !sleep(1))
        switch (plat) {
#if defined(__ARM_PCS_VFP)
            case HAL_PLATFORM_I3:  i3_config_load(app_config.sensor_config); break;
            case HAL_PLATFORM_I6:  i6_config_load(app_config.sensor_config); break;
            case HAL_PLATFORM_I6C: i6c_config_load(app_config.sensor_config); break;
            case HAL_PLATFORM_M6:  m6_config_load(app_config.sensor_config); break;
#elif defined(__arm__) && !defined(__ARM_PCS_VFP) && __ARM_ARCH == 6
            case HAL_PLATFORM_F1:  f1_config_load(app_config.sensor_config); break;
            case HAL_PLATFORM_F2:  f2_config_load(app_config.sensor_config); break;
#elif defined(__mips__)
            case HAL_PLATFORM_T31: t31_config_load(app_config.sensor_config); break;
#endif
        }

    HAL_INFO("media", "SDK has started successfully!\n");

    return EXIT_SUCCESS;
}

int sdk_stop(void) {
    pthread_join(vidPid, NULL);

    if (app_config.jpeg_enable)
        jpeg_deinit();

    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_I6:  i6_video_destroy_all(); break;
        case HAL_PLATFORM_I6C: i6c_video_destroy_all(); break;
        case HAL_PLATFORM_M6:  m6_video_destroy_all(); break;
        case HAL_PLATFORM_RK:  rk_video_destroy_all(); break;
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_AK:  ak_video_destroy_all(); break;
        case HAL_PLATFORM_GM:  gm_video_destroy_all(); break;
        case HAL_PLATFORM_V1:  v1_video_destroy_all(); break;
        case HAL_PLATFORM_V2:  v2_video_destroy_all(); break;
        case HAL_PLATFORM_V3:  v3_video_destroy_all(); break;
        case HAL_PLATFORM_V4:  v4_video_destroy_all(); break;
        #if __ARM_ARCH == 6
        case HAL_PLATFORM_F1:  f1_video_destroy_all(); break;
        case HAL_PLATFORM_F2:  f2_video_destroy_all(); break;
        #endif
#elif defined(__mips__)
        case HAL_PLATFORM_T31: t31_video_destroy_all(); break;
#elif defined(__riscv) || defined(__riscv__)
        case HAL_PLATFORM_CVI: cvi_video_destroy_all(); break;
#endif
    }

    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_I6:  i6_pipeline_destroy(); break;
        case HAL_PLATFORM_I6C: i6c_pipeline_destroy(); break;
        case HAL_PLATFORM_M6:  m6_pipeline_destroy(); break;
        case HAL_PLATFORM_RK:  rk_pipeline_destroy(); break;
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_AK:  ak_pipeline_destroy(); break;
        case HAL_PLATFORM_GM:  gm_pipeline_destroy(); break;
        case HAL_PLATFORM_V1:  v1_pipeline_destroy(); break;
        case HAL_PLATFORM_V2:  v2_pipeline_destroy(); break;
        case HAL_PLATFORM_V3:  v3_pipeline_destroy(); break;
        case HAL_PLATFORM_V4:  v4_pipeline_destroy(); break;
        #if __ARM_ARCH == 6
        case HAL_PLATFORM_F1:  f1_pipeline_destroy(); break;
        case HAL_PLATFORM_F2:  f2_pipeline_destroy(); break;
        #endif
#elif defined(__mips__)
        case HAL_PLATFORM_T31: t31_pipeline_destroy(); break;
#elif defined(__riscv) || defined(__riscv__)
        case HAL_PLATFORM_CVI: cvi_pipeline_destroy(); break;
#endif
    }

    if (app_config.audio_enable)
        media_audio_disable();

    if (isp_thread)
        pthread_join(ispPid, NULL);

    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_I3:  i3_system_deinit(); break;
        case HAL_PLATFORM_I6:  i6_system_deinit(); break;
        case HAL_PLATFORM_I6C: i6c_system_deinit(); break;
        case HAL_PLATFORM_M6:  m6_system_deinit(); break;
        case HAL_PLATFORM_RK:  rk_system_deinit(); break;
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_AK:  ak_system_deinit(); break;
        case HAL_PLATFORM_GM:  gm_system_deinit(); break;
        case HAL_PLATFORM_V1:  v1_system_deinit(); break;
        case HAL_PLATFORM_V2:  v2_system_deinit(); break;
        case HAL_PLATFORM_V3:  v3_system_deinit(); break;
        case HAL_PLATFORM_V4:  v4_system_deinit(); break;
        #if __ARM_ARCH == 6
        case HAL_PLATFORM_F1:  f1_system_deinit(); break;
        case HAL_PLATFORM_F2:  f2_system_deinit(); break;
        #endif
#elif defined(__mips__)
        case HAL_PLATFORM_T31: t31_system_deinit(); break;
#elif defined(__riscv) || defined(__riscv__)
        case HAL_PLATFORM_CVI: cvi_system_deinit(); break;
#endif
    }

    switch (plat) {
#if defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_V1: v1_sensor_deinit(); break;
        case HAL_PLATFORM_V2: v2_sensor_deinit(); break;
        case HAL_PLATFORM_V3: v3_sensor_deinit(); break;
        case HAL_PLATFORM_V4: v4_sensor_deinit(); break;
#endif
    }

    switch (plat) {
#if defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_I3:  i3_hal_deinit(); break;
        case HAL_PLATFORM_I6:  i6_hal_deinit(); break;
        case HAL_PLATFORM_I6C: i6c_hal_deinit(); break;
        case HAL_PLATFORM_M6:  m6_hal_deinit(); break;
        case HAL_PLATFORM_RK:  rk_hal_deinit(); break;
#elif defined(__arm__) && !defined(__ARM_PCS_VFP)
        case HAL_PLATFORM_AK:  ak_hal_deinit(); break;
        case HAL_PLATFORM_GM:  gm_hal_deinit(); break;
        case HAL_PLATFORM_V1:  v1_hal_deinit(); break;
        case HAL_PLATFORM_V2:  v2_hal_deinit(); break;
        case HAL_PLATFORM_V3:  v3_hal_deinit(); break;
        case HAL_PLATFORM_V4:  v4_hal_deinit(); break;
        #if __ARM_ARCH == 6
        case HAL_PLATFORM_F1:  f1_hal_deinit(); break;
        case HAL_PLATFORM_F2:  f2_hal_deinit(); break;
        #endif
#elif defined(__mips__)
        case HAL_PLATFORM_T31: t31_hal_deinit(); break;
#elif defined(__riscv) || defined(__riscv__)
        case HAL_PLATFORM_CVI: cvi_hal_deinit(); break;
#endif
    }

    HAL_INFO("media", "SDK had stopped successfully!\n");
    return EXIT_SUCCESS;
}
