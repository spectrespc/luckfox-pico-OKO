#!/usr/bin/env python3
"""End-to-end VPN (WireGuard / AmneziaWG) check on the bench DUT.

The script exercises the whole path: kernel module, the awg/wg binaries, the
import API, the helper, routes, the tunnel, the guard and the negative cases.
It needs an SSH account on the DUT and the WebUI credentials.

    bench/run.sh vpn_check.py --peer-host 192.168.52.1 \
        --profile output/bench/vpn/awg0.conf

Artifacts (report, SSH log, the imported profile) go to output/bench/vpn.
The --reboot step is opt-in because it restarts the DUT.
"""
import argparse
import base64
import json
import os
import shlex
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path

import paramiko

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT = ROOT / "output" / "bench" / "vpn"


class Check:
    def __init__(self):
        self.failures = 0
        self.steps = []

    def ok(self, name, detail=""):
        print(f"[ok]   {name}{(': ' + detail) if detail else ''}")
        self.steps.append({"name": name, "ok": True, "detail": detail})

    def fail(self, name, detail=""):
        self.failures += 1
        print(f"[FAIL] {name}{(': ' + detail) if detail else ''}")
        self.steps.append({"name": name, "ok": False, "detail": detail})


class Ssh:
    def __init__(self, host, user, password):
        self.host = host
        self.user = user
        self.password = password
        self.client = None
        self.log = []

    def connect(self, timeout=15):
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        client.connect(self.host, username=self.user, password=self.password,
                       timeout=timeout, banner_timeout=timeout)
        self.client = client
        return self

    def close(self):
        if self.client:
            self.client.close()
            self.client = None

    def run(self, command, timeout=60):
        if not self.client:
            self.connect()
        _, stdout, stderr = self.client.exec_command(command, timeout=timeout)
        out = stdout.read().decode(errors="replace")
        err = stderr.read().decode(errors="replace")
        rc = stdout.channel.recv_exit_status()
        self.log.append({"command": command, "rc": rc, "stdout": out,
                         "stderr": err})
        return rc, out + err


class Web:
    def __init__(self, host, user, password, timeout=20):
        self.base = f"http://{host}"
        token = base64.b64encode(f"{user}:{password}".encode()).decode()
        self.auth = f"Basic {token}"
        self.timeout = timeout

    def call(self, method, path, body=None, ctype=None):
        request = urllib.request.Request(self.base + path, data=body,
                                         method=method)
        request.add_header("Authorization", self.auth)
        if ctype:
            request.add_header("Content-Type", ctype)
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as resp:
                status, payload = resp.status, resp.read()
        except urllib.error.HTTPError as exc:
            status, payload = exc.code, exc.read()
        try:
            return status, json.loads(payload.decode(errors="replace"))
        except (ValueError, UnicodeDecodeError):
            return status, payload

    def vpn(self):
        return self.call("GET", "/api/vpn")

    def action(self, action, profile, token=""):
        fields = {"action": action, "profile": profile}
        if token:
            fields["token"] = token
        body = urllib.parse.urlencode(fields).encode()
        return self.call("POST", "/api/vpn", body,
                         "application/x-www-form-urlencoded")

    def import_profile(self, text, kind, user="", password=""):
        boundary = "----oko-vpn-" + uuid.uuid4().hex
        body = (
            f"--{boundary}\r\n"
            f'Content-Disposition: form-data; name="file"; filename="vpn.conf"\r\n'
            "Content-Type: text/plain\r\n\r\n"
        ).encode() + text.encode()
        if user or password:
            for name, value in (("user", user), ("pass", password)):
                body += (f"\r\n--{boundary}\r\n"
                         f'Content-Disposition: form-data; name="{name}"'
                         "\r\n\r\n" + value).encode()
        body += f"\r\n--{boundary}--\r\n".encode()
        return self.call("POST", f"/api/vpn/import?type={kind}", body,
                         f"multipart/form-data; boundary={boundary}")

    def import_text(self, text, kind="auto", user="", password=""):
        fields = {"text": text, "type": kind}
        if user or password:
            fields["user"] = user
            fields["pass"] = password
        body = urllib.parse.urlencode(fields).encode()
        return self.call("POST", "/api/vpn/import", body,
                         "application/x-www-form-urlencoded")



def profile_by_name(payload, name):
    for profile in payload.get("profiles", []):
        if profile.get("name") == name:
            return profile
    return None


def wait_state(web, name, wanted, timeout):
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            status, payload = web.vpn()
        except Exception:
            # Right after a reboot the HTTP server may be down or the route
            # may still be changing; keep polling.
            time.sleep(2)
            continue
        if status == 200 and isinstance(payload, dict):
            profile = profile_by_name(payload, name)
            last = profile
            if profile and profile.get("state") == wanted:
                return profile, payload
        time.sleep(2)
    return last, None


def wait_ssh(host, user, password, timeout, ssh):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            ssh.close()
            ssh.connect(timeout=10)
            rc, out = ssh.run("echo ready", timeout=10)
            if rc == 0 and "ready" in out:
                return True
        except Exception:
            pass
        time.sleep(3)
    return False


def first_target(allowedips, fulltunnel):
    for item in (allowedips or "").split(","):
        item = item.strip()
        if not item or item in ("0.0.0.0/0", "::/0") or ":" in item:
            continue
        address, _, prefix = item.partition("/")
        parts = address.split(".")
        if len(parts) != 4:
            continue
        try:
            octets = [int(part) for part in parts]
        except ValueError:
            continue
        if prefix and int(prefix) <= 30:
            octets[3] += 1
            if octets[3] > 254:
                octets[3] = 1
        return ".".join(str(part) for part in octets)
    return "1.1.1.1" if fulltunnel else None


def negative_configs():
    base = """[Interface]
PrivateKey = YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXo5
Address = 10.99.0.2/32

[Peer]
PublicKey = cGVlci1wdWJsaWMta2V5LTAwMDAwMDAwMDAwMDAwMDA9
Endpoint = 127.0.0.1:51820
AllowedIPs = 10.99.0.0/24
"""
    hook = base.replace("[Interface]\n",
                        "[Interface]\nPostUp = echo hook\n")
    full = base.replace("AllowedIPs = 10.99.0.0/24",
                        "AllowedIPs = 0.0.0.0/0")
    return hook, full


def ovpn_negative_configs():
    base = ("client\n"
            "dev tun\n"
            "proto udp\n"
            "remote 127.0.0.1 1194\n"
            "nobind\n"
            "auth-user-pass\n")
    plugin = base + "plugin /tmp/evil.so\n"
    full = base.replace("auth-user-pass\n", "") + "redirect-gateway\n"
    return plugin, full


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dut", default=os.environ.get("OKO_DUT_HOST",
                        "192.168.52.150"), help="DUT address")
    parser.add_argument("--peer-host", default=os.environ.get("OKO_VPN_PEER"),
                        help="VPN peer address on the host (informational)")
    parser.add_argument("--profile", help="local wg-quick .conf to import")
    parser.add_argument("--type", default="auto",
                        choices=["auto", "wg", "awg", "ovpn"])
    parser.add_argument("--creds-user", default=os.environ.get("OKO_VPN_USER",
                        ""), help="OpenVPN auth-user-pass user")
    parser.add_argument("--creds-pass", default=os.environ.get("OKO_VPN_PASS",
                        ""), help="OpenVPN auth-user-pass password")
    parser.add_argument("--iperf-peer", help="host running iperf3 -s for a "
                        "throughput check from the DUT")
    parser.add_argument("--user", default=os.environ.get("OKO_DUT_USER", "root"))
    parser.add_argument("--password", default=os.environ.get("OKO_DUT_PASSWORD",
                        "luckfox"))
    parser.add_argument("--web-user", default=os.environ.get("OKO_RTSP_USER",
                        "admin"))
    parser.add_argument("--web-password", default=os.environ.get("OKO_RTSP_PASS",
                        os.environ.get("OKO_DUT_PASSWORD", "oko-change-me")))
    parser.add_argument("--ping-target", help="address to ping through the tunnel")
    parser.add_argument("--timeout", type=int, default=120,
                        help="handshake timeout, seconds")
    parser.add_argument("--reboot", action="store_true",
                        help="also check autostart after a reboot")
    parser.add_argument("--keep", action="store_true",
                        help="keep the imported profile on the DUT")
    parser.add_argument("--out", default=str(DEFAULT_OUT))
    args = parser.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    check = Check()
    ssh = Ssh(args.dut, args.user, args.password)
    web = Web(args.dut, args.web_user, args.web_password)
    report = {"dut": args.dut, "peer_host": args.peer_host,
              "profile": args.profile, "type": args.type}

    try:
        ssh.connect()
        check.ok("ssh connected", args.dut)

        if args.type == "ovpn":
            rc, tun_text = ssh.run("test -c /dev/net/tun && echo tun-ok")
            if rc == 0:
                check.ok("/dev/net/tun")
            else:
                check.fail("/dev/net/tun", tun_text.strip())
            rc, ver_text = ssh.run("openvpn --version 2>&1 | head -n 1")
            if rc == 0 and "openvpn" in ver_text.lower():
                check.ok("openvpn version", ver_text.strip())
            else:
                check.fail("openvpn version", ver_text.strip())
        else:
            rc, out_text = ssh.run("lsmod | grep -q '^amneziawg' || "
                                   "insmod /oem/usr/ko/amneziawg.ko")
            rc2, mod_text = ssh.run("lsmod | grep '^amneziawg'")
            if rc2 == 0:
                check.ok("amneziawg module", mod_text.strip().splitlines()[0])
            else:
                check.fail("amneziawg module", out_text.strip())

            rc, awg_text = ssh.run("command -v awg >/dev/null 2>&1 && awg version || "
                                   "/oem/usr/bin/awg version")
            check.ok("awg version", awg_text.strip()) if rc == 0 else \
                check.fail("awg version", awg_text.strip())

        status, payload = web.vpn()
        if status == 200 and isinstance(payload, dict):
            available = payload.get("available", {})
            check.ok("GET /api/vpn", json.dumps(available))
            if not available.get("helper"):
                check.fail("oko-vpn helper", "not installed")
            if not (available.get("wg") or available.get("awg_bin") or
                    available.get("ovpn_bin")):
                check.fail("userspace tools", "wg/awg/openvpn not installed")
            token = payload.get("token", "")
        else:
            check.fail("GET /api/vpn", f"HTTP {status}")
            token = ""

        if not args.profile:
            check.fail("profile", "--profile is required for the tunnel steps")
            return finish(check, report, ssh, out)

        text = Path(args.profile).read_text()
        (out / "profile.conf").write_text(text)

        status, payload = web.import_profile(text, args.type,
                                            args.creds_user, args.creds_pass)
        if status == 200 and isinstance(payload, dict) and payload.get("imported"):
            name = payload.get("name", "")
            check.ok("profile imported", name)
        else:
            check.fail("profile imported", json.dumps(payload)[:300])
            return finish(check, report, ssh, out)

        status, payload = web.vpn()
        profile = profile_by_name(payload, name) if isinstance(payload, dict) else None
        if not profile:
            check.fail("profile listed", name)
            return finish(check, report, ssh, out)
        check.ok("profile listed", f"{profile.get('type')} {profile.get('ifname')}")

        status, payload = web.action("up", name)
        if not (status == 200 and isinstance(payload, dict) and not payload.get("error")):
            check.fail("connect request", json.dumps(payload)[:300])
            return finish(check, report, ssh, out)
        check.ok("connect request")

        profile, payload = wait_state(web, name, "up", args.timeout)
        if profile and profile.get("state") == "up":
            check.ok("interface up", profile.get("ifname"))
        else:
            check.fail("interface up", "timeout")
            return finish(check, report, ssh, out)

        ifname = profile.get("ifname")
        allowed = profile.get("allowedips", "")
        if args.type == "ovpn":
            rc, addr = ssh.run(f"ip -4 addr show dev {ifname}")
            if rc == 0 and "inet " in addr:
                check.ok("tunnel address", addr.split("inet ")[1].split()[0])
            else:
                check.fail("tunnel address", addr.strip()[-200:])
        else:
            rc, routes = ssh.run(f"ip route show dev {ifname}")
            missing = []
            for item in allowed.split(","):
                item = item.strip()
                if item and item not in ("0.0.0.0/0", "::/0") and item not in routes:
                    missing.append(item)
            if missing:
                check.fail("split routes", ", ".join(missing))
            else:
                check.ok("split routes", routes.strip().replace("\n", "; "))

        status, payload = web.vpn()
        live = (payload.get("live") or {}).get(ifname) if isinstance(payload, dict) else None
        if args.type != "ovpn":
            # A WireGuard/AmneziaWG handshake may take a few seconds after
            # the interface is reported up.
            for _ in range(6):
                age = live.get("handshake_age") if live else None
                if age is not None and age >= 0:
                    break
                time.sleep(2)
                status, payload = web.vpn()
                live = (payload.get("live") or {}).get(ifname) \
                    if isinstance(payload, dict) else None
        if live:
            check.ok("live counters", f"handshake_age={live.get('handshake_age')} "
                     f"rx={live.get('rx')} tx={live.get('tx')}")
            if args.type == "ovpn":
                if live.get("tunip"):
                    check.ok("live tunnel ip", live.get("tunip"))
                else:
                    check.fail("live tunnel ip", json.dumps(live)[:200])
            elif live.get("handshake_age", -1) is not None and live.get("handshake_age", -1) >= 0:
                check.ok("handshake")
            else:
                check.fail("handshake", "never")
        else:
            check.fail("live counters", "no live block")

        target = args.ping_target or first_target(allowed, profile.get("fulltunnel"))
        if target:
            rc, ping = ssh.run(f"ping -c 2 -W 2 {shlex.quote(target)}", timeout=20)
            check.ok("ping through tunnel", f"{target}") if rc == 0 else \
                check.fail("ping through tunnel", f"{target}: {ping.strip()[-200:]}")
        else:
            check.ok("ping through tunnel", "skipped: no target")

        if args.iperf_peer:
            rc, iperf = ssh.run(f"iperf3 -c {shlex.quote(args.iperf_peer)} -t 5",
                                timeout=40)
            if rc == 0:
                line = [l for l in iperf.splitlines() if "sender" in l]
                check.ok("iperf3 throughput", line[-1].strip() if line else "ok")
            else:
                check.fail("iperf3 throughput", iperf.strip()[-200:])

        status, payload = web.action("down", name)
        if status == 200 and isinstance(payload, dict) and not payload.get("error"):
            check.ok("disconnect request")
        else:
            check.fail("disconnect request", json.dumps(payload)[:300])
        profile, _ = wait_state(web, name, "down", 30)
        if profile and profile.get("state") == "down":
            check.ok("interface down")
        else:
            check.fail("interface down", "still up")
        rc, routes = ssh.run(f"ip route show dev {ifname} 2>/dev/null | wc -l")
        check.ok("routes removed") if routes.strip() == "0" else \
            check.fail("routes removed", routes.strip())

        # Negative checks: hooks and a delete without the token must be blocked.
        if args.type == "ovpn":
            hook, full = ovpn_negative_configs()
            status, payload = web.import_text(hook, "ovpn",
                                              args.creds_user, args.creds_pass)
            reason = payload.get("reason", "") if isinstance(payload, dict) else ""
            if "not supported" in reason:
                check.ok("unsafe openvpn directive rejected")
            else:
                check.fail("unsafe openvpn directive rejected",
                           json.dumps(payload)[:300])
            status, payload = web.import_text(
                "client\ndev tun\nremote 127.0.0.1 1194\nauth-user-pass\n",
                "ovpn")
            reason = payload.get("reason", "") if isinstance(payload, dict) else ""
            if "credentials required" in reason:
                check.ok("missing credentials rejected")
            else:
                check.fail("missing credentials rejected",
                           json.dumps(payload)[:300])
        else:
            hook, full = negative_configs()
            status, payload = web.import_text(hook)
            reason = payload.get("reason", "") if isinstance(payload, dict) else ""
            if "script hooks" in reason:
                check.ok("hook config rejected")
            else:
                check.fail("hook config rejected", json.dumps(payload)[:300])

        status, payload = web.action("delete", name)
        reason = payload.get("reason", "") if isinstance(payload, dict) else ""
        if "token" in reason:
            check.ok("delete without token rejected")
        else:
            check.fail("delete without token rejected", json.dumps(payload)[:300])

        # A full tunnel is allowed by R2 and must be reported as such.
        status, payload = web.import_text(full, args.type, args.creds_user,
                                          args.creds_pass)
        if status == 200 and isinstance(payload, dict) and payload.get("imported"):
            full_name = payload.get("name", "")
            status, payload = web.vpn()
            full_profile = profile_by_name(payload, full_name) \
                if isinstance(payload, dict) else None
            if full_profile and full_profile.get("fulltunnel"):
                check.ok("full tunnel accepted", full_name)
            else:
                check.fail("full tunnel flag", json.dumps(full_profile)[:200])
            if full_name == name:
                # The full-tunnel probe replaced the profile under test;
                # restore the original file so the cleanup below works.
                web.import_profile(text, args.type, args.creds_user,
                                   args.creds_pass)
            else:
                web.action("delete", full_name, token)
        else:
            check.fail("full tunnel import", json.dumps(payload)[:300])

        if args.reboot:
            status, payload = web.action("enable", name)
            if not (status == 200 and isinstance(payload, dict) and not payload.get("error")):
                check.fail("autostart enable", json.dumps(payload)[:200])
            ssh.run("sync; reboot", timeout=10)
            ssh.close()
            if wait_ssh(args.dut, args.user, args.password, 240, ssh):
                check.ok("rebooted")
                profile, _ = wait_state(web, name, "up", 180)
                if profile and profile.get("state") == "up":
                    check.ok("autostart after reboot")
                else:
                    check.fail("autostart after reboot", "interface is down")
            else:
                check.fail("rebooted", "SSH did not come back")
            web.action("disable", name)

        if not args.keep:
            status, payload = web.vpn()
            token = payload.get("token", "") if isinstance(payload, dict) else token
            web.action("delete", name, token)
            status, payload = web.vpn()
            if isinstance(payload, dict) and not profile_by_name(payload, name):
                check.ok("profile deleted")
            else:
                check.fail("profile deleted")

        return finish(check, report, ssh, out)
    except Exception as exc:  # noqa: BLE001 - the script reports, not raises
        check.fail("unexpected error", repr(exc))
        return finish(check, report, ssh, out)


def finish(check, report, ssh, out):
    report["checks"] = check.steps
    report["failures"] = check.failures
    (out / "report.json").write_text(json.dumps(report, indent=2))
    (out / "ssh.log").write_text(json.dumps(ssh.log, indent=2))
    ssh.close()
    print(f"\n{len(check.steps) - check.failures}/{len(check.steps)} checks passed")
    print(f"artifacts: {out}")
    return 1 if check.failures else 0


if __name__ == "__main__":
    sys.exit(main())