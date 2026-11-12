#pragma once

#include <stddef.h>

/* OKO Wi-Fi (USB adapters), backed by /usr/bin/oko-wifi.
 *
 * The helper stores the settings in the U-Boot env (oko_wlan_*) and owns the
 * wlan0 state machine.  divinus only validates arguments and serialises the
 * helper output, so the WebUI never needs to know which chip is attached. */

/* GET /api/wifi: settings and live state as JSON.  Returns 0 on success. */
size_t wifi_status_json(char *buffer, size_t capacity);

/* GET /api/wifi/scan: cached scan result (or an idle object). */
size_t wifi_scan_json(char *buffer, size_t capacity);

/* GET /api/wifi/apinfo: SoftAP SSID, passphrase, address and state. */
size_t wifi_apinfo_json(char *buffer, size_t capacity);

/* POST /api/wifi?scan=1: start a background scan.  Returns 0 when started. */
int wifi_scan_start(char *error, size_t error_size);

/* POST /api/wifi: validate and store the settings.  Empty psk/ap_psk mean
 * "keep the stored passphrase"; an empty metric keeps the stored priority.
 * The server applies the change after the HTTP response has been sent.
 * Returns 0, or -1 invalid, -2 AP+STA unsupported, -3 the U-Boot environment
 * could not be written. */
int wifi_save(const char *mode, const char *ssid, const char *psk,
    const char *dhcp, const char *ip, const char *mask, const char *gateway,
    const char *dns, const char *ap_ssid, const char *ap_psk,
    const char *metric, char *error, size_t error_size);

/* POST /api/wifi?metric=...: store only the uplink priority.  The routing
 * daemon reads it on its next pass.  Returns 0, -1 invalid, -3 env error. */
int wifi_save_metric(const char *metric, char *error, size_t error_size);

/* True when the helper is installed. */
int wifi_available(void);