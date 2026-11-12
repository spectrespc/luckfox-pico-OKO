#!/usr/bin/env python3
"""Soak the dual-uplink failover: Ethernet link, Wi-Fi gateway, both defaults.

The DUT must already have both uplinks up (Ethernet on the lab LAN and wlan0
in STA mode on the temporary bench AP, see TESTBENCH.md §9).  The script:

  0-4 min   both links up, the default must be eth0
  4-8 min   eth0 link down, the default must move to wlan0
  8-12 min  eth0 link up, the default must return to eth0
  12-16 min the Wi-Fi gateway is dropped with an iptables INPUT rule on the
            bench AP, wlan0 is demoted and eth0 stays the default
  16-20 min both links up again, the default must be eth0

Every 30 s it checks HTTP on the DUT's Ethernet and Wi-Fi addresses, the
default route chosen for 8.8.8.8 and that divinus is still running.  The
Ethernet HTTP check is skipped while the link is intentionally down.

The AP gateway drop runs in the `oko-services` container (hostapd on the host
wlan0, host networking); iptables must be installed there.  The temporary AP
is a test aid and is not part of the image.

    bench/run.sh uplink_soak.py
    bench/run.sh uplink_soak.py --dut 192.168.44.50 --eth 192.168.52.33 \
        --no-eth-toggle

Environment: OKO_DUT_PASSWORD, OKO_UPLINK_DUT, OKO_UPLINK_ETH,
OKO_UPLINK_AP_IF, OKO_UPLINK_AP_IP.
"""
import argparse
import base64
import os
import subprocess
import time
import urllib.request

import paramiko

PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "luckfox")
USER = os.environ.get("OKO_DUT_USER", "root")
WEB_USER = os.environ.get("OKO_RTSP_USER", "admin")
AUTH = base64.b64encode(
    (WEB_USER + ":" + os.environ.get("OKO_RTSP_PASS", "oko-change-me")).encode()).decode()
CONTAINER = os.environ.get("OKO_SERVICES_CONTAINER", "oko-services")


def ssh(host, command, timeout=30):
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(host, username=USER, password=PASSWORD, timeout=8,
                   banner_timeout=8)
    _, stdout, _ = client.exec_command(command, timeout=timeout)
    text = stdout.read().decode(errors="replace")
    client.close()
    return text


def http(ip):
    request = urllib.request.Request(
        "http://%s/api/status" % ip,
        headers={"Authorization": "Basic " + AUTH})
    try:
        with urllib.request.urlopen(request, timeout=8) as response:
            return response.status
    except Exception:
        return 0


def route_dev(host):
    try:
        text = ssh(host, "ip route get 8.8.8.8 2>/dev/null | head -1")
    except Exception:
        return "none"
    return text.split(" dev ")[1].split()[0] if " dev " in text else "none"


def wait_route(host, expected, timeout=25):
    """Let oko-route and DHCP converge after a link or gateway change."""
    deadline = time.time() + timeout
    dev = route_dev(host)
    while dev != expected and time.time() < deadline:
        time.sleep(2)
        dev = route_dev(host)
    return dev


def docker(command):
    return subprocess.run(
        ["docker", "exec", CONTAINER, "sh", "-c", command],
        capture_output=True, text=True).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dut", default=os.environ.get("OKO_UPLINK_DUT",
                                                        "192.168.44.50"),
                        help="DUT address reachable over Wi-Fi (control)")
    parser.add_argument("--eth", default=os.environ.get("OKO_UPLINK_ETH",
                                                        "192.168.52.33"),
                        help="DUT address on the lab LAN")
    parser.add_argument("--ap-if", default=os.environ.get("OKO_UPLINK_AP_IF",
                                                          "wlan0"))
    parser.add_argument("--ap-ip", default=os.environ.get("OKO_UPLINK_AP_IP",
                                                          "192.168.44.1"))
    parser.add_argument("--seconds", type=int, default=1200,
                        help="total soak time (default 1200 = 20 min)")
    parser.add_argument("--interval", type=int, default=30)
    parser.add_argument("--no-eth-toggle", action="store_true",
                        help="keep the Ethernet link up")
    parser.add_argument("--no-ap-toggle", action="store_true",
                        help="never drop the Wi-Fi gateway")
    args = parser.parse_args()

    rule = ("iptables -I INPUT -i %s -s %s -d %s -p icmp "
            "--icmp-type echo-request -j DROP" %
            (args.ap_if, args.dut, args.ap_ip))
    rule_del = rule.replace("-I INPUT", "-D INPUT") + " || true"
    total = max(args.seconds, args.interval)
    steps = total // args.interval
    started = time.time()
    failures = checks = 0
    try:
        for index in range(steps + 1):
            minute = (time.time() - started) / 60.0
            expect = "eth0"
            if not args.no_eth_toggle and 4 <= minute < 8:
                if index == max(1, int(4 * 60 / args.interval)):
                    ssh(args.dut, "ip link set eth0 down")
                expect = "wlan0"
            elif not args.no_eth_toggle and 8 <= minute < 12:
                if index == max(1, int(8 * 60 / args.interval)):
                    ssh(args.dut, "/usr/bin/oko-net apply")
            elif not args.no_ap_toggle and 12 <= minute < 16:
                if index == max(1, int(12 * 60 / args.interval)):
                    docker(rule)
            else:
                if not args.no_ap_toggle and minute >= 16:
                    if index == max(1, int(16 * 60 / args.interval)):
                        docker(rule_del)
                    if not args.no_eth_toggle and index == max(
                            2, int(16.5 * 60 / args.interval)):
                        ssh(args.dut, "/usr/bin/oko-net apply")

            dev = wait_route(args.dut, expect)
            eth_status = 0 if expect == "wlan0" else http(args.eth)
            wlan_status = http(args.dut)
            try:
                pid = ssh(args.dut, "pidof divinus").strip()
            except Exception:
                pid = ""
            ok = (wlan_status == 200 and pid and dev == expect and
                  (expect == "wlan0" or eth_status == 200))
            checks += 1
            failures += 0 if ok else 1
            print("%02d min=%5.1f expect=%s eth=%s wlan=%s default=%s "
                  "divinus=%s %s" %
                  (index, minute, expect, eth_status or "down", wlan_status,
                   dev, pid or "DOWN", "OK" if ok else "FAIL"), flush=True)
            time.sleep(args.interval)
    finally:
        docker(rule_del)
        try:
            ssh(args.dut, "ip link set eth0 up; /usr/bin/oko-net apply")
        except Exception:
            pass
        print("soak done: %d checks, %d failures" % (checks, failures),
              flush=True)
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
