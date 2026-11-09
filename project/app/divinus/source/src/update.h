#pragma once

/* OKO software update through the WebUI.
 *
 * The device never writes the SPI NAND from Linux.  A package is staged on the
 * microSD card together with sd_update.txt, the apply step arms a U-Boot env
 * flag and reboots, and U-Boot's sd_update command flashes boot/oem/rootfs.
 * The HTTP thread only validates requests and starts a worker; hashing and
 * unpacking 86 MiB must not block every other client. */

int update_init(void);
void update_shutdown(void);
int update_busy(void);

/* GET /api/update: a malloc()ed JSON snapshot (state, staged package, token).
 * NULL means out of memory. */
char *update_json(void);

/* Streaming upload support for POST /api/update/upload.  The body is written
 * straight to the microSD card, bypassing the 4 MiB request buffer.
 * update_upload_open() returns 0, or -1 when the caller must answer with the
 * reason from update_upload_error(). */
int update_upload_open(long long total);
const char *update_upload_error(void);
long long update_upload_write(const char *data, long long len);
int update_upload_finish(void);
void update_upload_abort(const char *reason);
/* Set by POST /api/update?cancel=1; the server stops the upload stream. */
int update_upload_cancelled(void);

/* POST /api/update/fetch?url=...: download with curl and stage. */
int update_fetch_start(const char *url, char *err, unsigned int err_size);
/* POST /api/update/apply: confirm=APPLY and the token from GET /api/update. */
int update_apply_start(const char *confirm, const char *token, char *err,
                       unsigned int err_size);
/* POST /api/update?clear=1: drop the staged card files. */
int update_clear(char *err, unsigned int err_size);
/* POST /api/update?cancel=1: stop the running worker or upload.  An armed
 * apply is not cancellable: returns -1 in that case. */
int update_cancel(void);