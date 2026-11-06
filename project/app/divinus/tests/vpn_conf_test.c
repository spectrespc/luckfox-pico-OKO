/*
 * Host test for the VPN configuration parser shared by the API and the
 * import path.  The parser is included directly so the test needs no extra
 * object files.
 *
 * Run from the repository root:
 *   gcc -std=c99 -Wall -Wextra -O2 project/app/divinus/tests/vpn_conf_test.c \
 *       -o /tmp/vpn_conf_test && /tmp/vpn_conf_test
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>

#include "../source/src/vpn_conf.h"
#include "../source/src/vpn_conf.c"

static int failures;

static void check(int condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s\n", name);
        failures++;
    }
}

static int parse(const char *text, vpn_conf_t *conf, char *error,
    size_t error_size)
{
    return vpn_conf_parse_text(text, strlen(text), conf, error, error_size);
}

int main(void)
{
    char error[VPN_ERROR_MAX];
    char buffer[VPN_CONF_MAX_BYTES + VPN_CONF_LINE_MAX];
    vpn_conf_t conf;

    static const char *wg_conf =
        "# oko: type=wg auto=1 mtu=1380 dns=keep\n"
        "[Interface]\n"
        "PrivateKey = aGVsbG8td29ybGQtcHJpdmF0ZS1rZXktMDAwMDA9\n"
        "Address = 10.8.0.2/24, fd00::2/64\n"
        "DNS = 10.8.0.1\n"
        "MTU = 1380\n"
        "\n"
        "[Peer]\n"
        "PublicKey = cGVlci1wdWJsaWMta2V5LTAwMDAwMDAwMDAwMDAwMDA9\n"
        "Endpoint = vpn.example.com:51820\n"
        "AllowedIPs = 10.8.0.0/24, 192.168.52.0/24\n"
        "PersistentKeepalive = 25\n";

    static const char *awg_conf =
        "[Interface]\n"
        "PrivateKey = YW1uZXppYS1wcml2YXRlLWtleS0wMDAwMDAwMDAwMDA9\n"
        "Address = 10.9.0.2/32\n"
        "Jc = 5\n"
        "Jmin = 50\n"
        "Jmax = 1000\n"
        "S1 = 20\n"
        "S2 = 30\n"
        "S3 = 40\n"
        "S4 = 50\n"
        "H1 = 1111111\n"
        "I1 = <b 0x01000000>\n"
        "I5 = <r 10>\n"
        "HeaderProtectionKey = aGVhZGVyLXByb3RlY3Rpb24ta2V5LTAwMDAwMDAwPQ==\n"
        "ContentPaddingAddition = 10\n"
        "\n"
        "[Peer]\n"
        "PublicKey = cGVlci1wdWJsaWMta2V5LTAwMDAwMDAwMDAwMDAwMDA9\n"
        "Endpoint = awg.example.com:443\n"
        "AllowedIPs = 0.0.0.0/0\n"
        "AdvancedSecurity = on\n";

    static const char *hook_conf =
        "[Interface]\n"
        "PrivateKey = YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXo9\n"
        "PostUp = iptables -A FORWARD -j ACCEPT\n"
        "\n"
        "[Peer]\n"
        "PublicKey = cGVlci1wdWJsaWMta2V5LTAwMDAwMDAwMDAwMDAwMDA9\n"
        "Endpoint = 1.2.3.4:51820\n"
        "AllowedIPs = 10.0.0.0/24\n";

    static const char *no_private_conf =
        "[Interface]\n"
        "Address = 10.0.0.2/24\n"
        "\n"
        "[Peer]\n"
        "PublicKey = cGVlci1wdWJsaWMta2V5LTAwMDAwMDAwMDAwMDAwMDA9\n"
        "Endpoint = 1.2.3.4:51820\n";

    static const char *bad_endpoint_conf =
        "[Interface]\n"
        "PrivateKey = YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXo9\n"
        "\n"
        "[Peer]\n"
        "PublicKey = cGVlci1wdWJsaWMta2V5LTAwMDAwMDAwMDAwMDAwMDA9\n"
        "Endpoint = 1.2.3.4:notaport\n";

    static const char *bad_protection_conf =
        "[Interface]\n"
        "PrivateKey = YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXo9\n"
        "S1 = 20\n"
        "S2 = 30\n"
        "S3 = 40\n"
        "HeaderProtectionKey = aGVhZGVyLXByb3RlY3Rpb24ta2V5LTAwMDAwMDAwPQ==\n"
        "\n"
        "[Peer]\n"
        "PublicKey = cGVlci1wdWJsaWMta2V5LTAwMDAwMDAwMDAwMDAwMDA9\n"
        "Endpoint = 1.2.3.4:51820\n"
        "AllowedIPs = 10.0.0.0/24\n";

    static const char *no_peer_conf =
        "[Interface]\n"
        "PrivateKey = YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXo9\n"
        "Address = 10.0.0.2/24\n";

    /* A valid WireGuard profile with metadata. */
    check(parse(wg_conf, &conf, error, sizeof(error)) == 0, "wg accepted");
    check(!strcmp(conf.type, "wg"), "wg type");
    check(!strcmp(conf.ifname, "wg0"), "wg ifname");
    check(!strcmp(conf.address4, "10.8.0.2/24"), "wg address4");
    check(!strcmp(conf.address6, "fd00::2/64"), "wg address6");
    check(!strcmp(conf.endpoint, "vpn.example.com:51820"), "wg endpoint");
    check(!strcmp(conf.allowedips, "10.8.0.0/24,192.168.52.0/24"),
        "wg allowedips");
    check(conf.mtu == 1380, "wg mtu from metadata");
    check(conf.auto_start == 1, "wg auto");
    check(conf.fulltunnel == 0, "wg split tunnel");
    check(conf.dns_tunnel == 0, "wg dns keep");

    /* AWG-specific keys select the amneziawg type and a full tunnel. */
    check(parse(awg_conf, &conf, error, sizeof(error)) == 0, "awg accepted");
    check(!strcmp(conf.type, "awg"), "awg type inferred");
    check(!strcmp(conf.ifname, "awg0"), "awg ifname");
    check(conf.fulltunnel == 1, "awg full tunnel");
    check(conf.mtu == 1420, "awg default mtu");

    /* Configuration text survives a metadata rewrite byte for byte. */
    snprintf(buffer, sizeof(buffer), "%s", awg_conf);
    check(vpn_conf_set_type(buffer, sizeof(buffer), "awg") == 0,
        "awg set type");
    check(strstr(buffer, "I1 = <b 0x01000000>") != NULL,
        "awg I1 spaces preserved");
    check(strstr(buffer, "I5 = <r 10>") != NULL, "awg I5 spaces preserved");
    check(strstr(buffer, "# oko: type=awg") != NULL, "awg metadata added");

    /* Replacing an existing type token keeps the other metadata. */
    snprintf(buffer, sizeof(buffer), "%s", wg_conf);
    check(vpn_conf_set_type(buffer, sizeof(buffer), "awg") == 0,
        "wg set type");
    check(strstr(buffer, "# oko: auto=1 mtu=1380 dns=keep type=awg") != NULL,
        "metadata type replaced");
    check(!strstr(buffer, "type=wg"), "old type removed");
    check(parse(buffer, &conf, error, sizeof(error)) == 0, "rewritten parses");
    check(!strcmp(conf.type, "awg"), "rewritten type");

    /* Script hooks are rejected. */
    check(parse(hook_conf, &conf, error, sizeof(error)) != 0, "hook rejected");
    check(strstr(error, "script hooks are not supported") != NULL,
        "hook reason");

    /* Required fields. */
    check(parse(no_private_conf, &conf, error, sizeof(error)) != 0,
        "no private rejected");
    check(parse(bad_endpoint_conf, &conf, error, sizeof(error)) != 0,
        "bad endpoint rejected");
    check(parse(no_peer_conf, &conf, error, sizeof(error)) != 0,
        "no peer rejected");
    check(parse(bad_protection_conf, &conf, error, sizeof(error)) != 0,
        "header protection without S4 rejected");
    check(strstr(error, "S4") != NULL, "header protection S4 reason");

    /* Profile names. */
    check(vpn_conf_valid_name("wg0") == 1, "name wg0");
    check(vpn_conf_valid_name("awg-main_1") == 1, "name awg-main_1");
    check(vpn_conf_valid_name("../etc") == 0, "name traversal");
    check(vpn_conf_valid_name("") == 0, "name empty");
    check(vpn_conf_valid_name("UPPER") == 0, "name upper");

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("ok vpn_conf\n");
    return 0;
}