#pragma once

#include <stddef.h>

/* Controller firmware update jobs.
 *
 * The HTTP thread only validates the upload and starts a worker; the worker
 * owns the S21oko-ec stop/start sequence and focus-motor suspension because a
 * synchronous flash would block every other HTTP client. */

int flash_init(void);
void flash_shutdown(void);
int flash_busy(void);

/* Handles POST /api/flash (multipart/form-data).  Returns a malloc()ed JSON
 * body and sets *http_status.  NULL means an internal failure. */
char *flash_post(const char *content_type, char *payload, int body_len,
                 int *http_status);

/* Returns a malloc()ed JSON object for GET /api/flash. */
char *flash_json(void);