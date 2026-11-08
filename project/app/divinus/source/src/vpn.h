#pragma once

#include <stddef.h>

#include "vpn_conf.h"

/* Builds the JSON body of GET /api/vpn.  Returns the body length, or 0 when
 * the buffer is too small. */
size_t vpn_status_json(char *buffer, size_t capacity);

/* Saves an imported configuration as /userdata/vpn/<ifname>.conf.
 * "type_hint" is "auto", "wg", "awg" or "ovpn" (empty means auto).
 * "creds_user"/"creds_pass" are used only by an OpenVPN profile with a bare
 * "auth-user-pass" and are stored in /userdata/vpn/<ifname>.pass (0600).
 * Returns 0 and the profile name on success. */
int vpn_import_text(const char *text, size_t length, const char *type_hint,
    const char *creds_user, const char *creds_pass,
    char *name, size_t name_size, char *error, size_t error_size);

/* Performs up/down/enable/disable/delete.  "delete" requires the token shown
 * by vpn_status_json(). */
int vpn_action(const char *action, const char *profile, const char *token,
    char *error, size_t error_size);

/* Resolves a profile file for export; refuses symlinks and bad names. */
int vpn_export_path(const char *profile, char *path, size_t path_size,
    char *error, size_t error_size);

const char *vpn_token(void);

/* True when the helper and at least one userspace tool are present. */
int vpn_available(void);