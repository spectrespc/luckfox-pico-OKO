#ifndef __RTSP_SERVER_H
#define __RTSP_SERVER_H

#if defined (__cplusplus)
extern "C" {
#endif

#include <unistd.h>
#include <time.h>

/******************************************************************************
 *              DEFINITIONS
 ******************************************************************************/
#define SERVER_RTP_PORT 5004
#define SERVER_RTCP_PORT 5025
#define RTSP_MAXIMUM_FRAMERATE 60
#define RTSP_MAXIMUM_CONNECTIONS 16
/* Stream 0 is the main (/live/0) stream; the configured sub stream is 1. */
#define RTSP_MAX_STREAMS 2

#define STR_RTSP_VERSION "RTSP/1.0"

/* __rtsp_obj_t is private. you will not see it */
typedef struct __rtsp_obj_t *rtsp_handle;

/******************************************************************************
 *              LIBRARY FUNCTIONS
 ******************************************************************************/
/* put virtual pointer to 'buf', which consists of 1 or more NALUs (start code required).
   SPS and PPS parameters are automatically collected during execution. */

#include "../hal/types.h"

void rtp_disable_audio(rtsp_handle h);
int rtp_send_h26x(rtsp_handle h, int stream_id, hal_vidstream *stream, char isH265);
int rtp_send_mp3(rtsp_handle h, unsigned char *buf, size_t len);
int rtp_send_pcma(rtsp_handle h, unsigned char *buf, size_t len);
int rtp_send_pcmu(rtsp_handle h, unsigned char *buf, size_t len);

extern void rtsp_finish(rtsp_handle h);

extern rtsp_handle rtsp_create(unsigned char max_con, unsigned int port, int priority);

/* Registers an additional RTSP path.  Returns its stream id or -1. */
extern int rtsp_add_stream(rtsp_handle h, const char *path);
/* Binds a stream to the VENC channel that produces it (or -1 to unbind). */
extern int rtsp_set_stream_channel(rtsp_handle h, int stream_id, int channel);
extern int rtsp_get_stream_channel(rtsp_handle h, int stream_id);
extern int rtsp_set_stream_path(rtsp_handle h, int stream_id, const char *path);
extern int rtsp_disable_stream(rtsp_handle h, int stream_id);
extern int rtsp_get_connection_count(rtsp_handle h);

extern void rtsp_configure_auth(rtsp_handle h, const char *user, const char *pass);

extern int rtsp_tick(rtsp_handle h);

#if defined (__cplusplus)
}
#endif

#endif
