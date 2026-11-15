#!/usr/bin/env python3
"""Probe the mDNS publication of the bench DUT with a raw UDP query.

The script sends an A-record question for ``<name>.local`` to
``224.0.0.251:5353``, parses the response and compares the address with the
DUT state.  It uses the standard library only and does not need avahi or
nss-mdns on the host, so it is the source of truth for the feature gate.

    bench/run.sh mdns_check.py                       # name from GET /api/mdns
    bench/run.sh mdns_check.py oko-e80e              # explicit name (no .local)
    bench/run.sh mdns_check.py --ip 192.168.52.33    # explicit expected address
    bench/run.sh mdns_check.py --absent              # after POST ?enable=0

Exit codes: 0 - the expected answer arrived (or none was expected),
1 - the answer is missing or differs from the DUT, 2 - bad arguments.
"""
import argparse
import base64
import json
import os
import socket
import struct
import sys
import time
import urllib.error
import urllib.request

MDNS_GROUP = "224.0.0.251"
MDNS_PORT = 5353
QTYPE_A = 1
QCLASS_IN = 1
FLAG_RESPONSE = 0x8000

HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.33")
USER = os.environ.get("OKO_RTSP_USER", "admin")
PASSWORD = os.environ.get("OKO_DUT_PASSWORD", "oko-change-me")


def api_state(host):
    """GET /api/mdns; returns {} when the endpoint is unreachable."""
    req = urllib.request.Request("http://%s/api/mdns" % host)
    token = base64.b64encode(("%s:%s" % (USER, PASSWORD)).encode()).decode()
    req.add_header("Authorization", "Basic " + token)
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return json.loads(resp.read().decode())
    except (urllib.error.URLError, OSError, ValueError):
        return {}


def encode_name(name):
    out = b""
    for label in name.rstrip(".").split("."):
        raw = label.encode("ascii")
        if not raw or len(raw) > 63:
            raise ValueError("bad DNS label %r" % label)
        out += bytes([len(raw)]) + raw
    return out + b"\x00"


def build_query(name):
    header = struct.pack(">HHHHHH", 0, 0, 1, 0, 0, 0)
    return header + encode_name(name) + struct.pack(">HH", QTYPE_A, QCLASS_IN)


def read_name(packet, offset):
    """Decodes a (possibly compressed) DNS name; returns (name, next offset)."""
    labels = []
    end = None
    seen = set()

    while True:
        if offset >= len(packet):
            raise ValueError("truncated name")
        length = packet[offset]
        if length & 0xC0 == 0xC0:
            if offset + 1 >= len(packet):
                raise ValueError("truncated pointer")
            pointer = ((length & 0x3F) << 8) | packet[offset + 1]
            if end is None:
                end = offset + 2
            if pointer in seen:
                raise ValueError("name pointer loop")
            seen.add(pointer)
            offset = pointer
            continue
        if length & 0xC0:
            raise ValueError("unsupported label type")
        offset += 1
        if length == 0:
            if end is None:
                end = offset
            break
        if offset + length > len(packet):
            raise ValueError("truncated label")
        labels.append(packet[offset:offset + length].decode("ascii", "replace"))
        offset += length
    return ".".join(labels), end


def parse_response(packet):
    """Returns [(name, address)] for every A record in a DNS response."""
    if len(packet) < 12:
        return []
    _ident, flags, questions, answers, _auth, _add = struct.unpack(
        ">HHHHHH", packet[:12])
    if not flags & FLAG_RESPONSE:
        return []

    offset = 12
    for _ in range(questions):
        _name, offset = read_name(packet, offset)
        offset += 4

    found = []
    for _ in range(answers):
        name, offset = read_name(packet, offset)
        if offset + 10 > len(packet):
            break
        rtype, _rclass, _ttl, rdlen = struct.unpack(
            ">HHIH", packet[offset:offset + 10])
        offset += 10
        if offset + rdlen > len(packet):
            break
        if rtype == QTYPE_A and rdlen == 4:
            found.append((name, socket.inet_ntoa(packet[offset:offset + 4])))
        offset += rdlen
    return found


def probe(name, timeout, interface):
    """Sends the query every second and returns {address: source address}.

    tinysvcmdns answers by multicast to the group, never directly to the
    sender, so the socket must listen on 5353 and join the group.  Multiple
    listeners are allowed through SO_REUSEADDR (avahi keeps working).
    """
    query = build_query(name)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, "SO_REUSEPORT"):
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except OSError:
            pass
    sock.bind(("", MDNS_PORT))
    group = socket.inet_aton(MDNS_GROUP)
    mreq = struct.pack("4s4s", group,
                       socket.inet_aton(interface) if interface else b"\x00" * 4)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    if interface:
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                        socket.inet_aton(interface))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 0)

    wanted = name.rstrip(".").lower()
    answers = {}
    deadline = time.time() + timeout
    try:
        while time.time() < deadline:
            sock.sendto(query, (MDNS_GROUP, MDNS_PORT))
            until = min(deadline, time.time() + 1.0)
            while time.time() < until:
                sock.settimeout(max(0.05, until - time.time()))
                try:
                    packet, source = sock.recvfrom(9000)
                except socket.timeout:
                    break
                for answer_name, address in parse_response(packet):
                    if answer_name.rstrip(".").lower() != wanted:
                        continue
                    answers[address] = source[0]
            if answers:
                break
    finally:
        sock.close()
    return answers


def local_address(host):
    """Local address of the route towards the DUT (for the multicast send)."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect((host, 9))
        return sock.getsockname()[0]
    except OSError:
        return None
    finally:
        sock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("name", nargs="?",
                        help="expected name; defaults to GET /api/mdns")
    parser.add_argument("--ip", action="append", default=[],
                        help="expected IPv4 address (repeatable)")
    parser.add_argument("-H", "--host", default=HOST,
                        help="DUT address for the API cross-check")
    parser.add_argument("--timeout", type=float, default=5.0,
                        help="how long to listen for an answer (seconds)")
    parser.add_argument("--iface", default=None,
                        help="local interface address for multicast (default: "
                             "the route towards the DUT)")
    parser.add_argument("--absent", action="store_true",
                        help="require no answer (publication is disabled)")
    args = parser.parse_args()

    state = api_state(args.host)
    name = args.name or state.get("name") or ""
    if not name:
        print("FAIL: no mDNS name given and GET /api/mdns is unreachable")
        return 2
    if not name.endswith(".local"):
        name += ".local"

    expected = set(args.ip)
    if not expected and not args.absent:
        expected = set(state.get("addresses") or []) or {args.host}
    print("query %s -> %s:%d (timeout %.1fs)" %
          (name, MDNS_GROUP, MDNS_PORT, args.timeout))

    interface = args.iface or local_address(args.host)
    try:
        answers = probe(name, args.timeout, interface)
    except OSError as err:
        print("FAIL: cannot listen on UDP %d: %s" %
              (MDNS_PORT, err))
        return 2
    for address in sorted(answers):
        print("answer from %s: %s A %s" % (answers[address], name, address))

    if args.absent:
        if answers:
            print("FAIL: %s still answers with %s" %
                  (name, ", ".join(sorted(answers))))
            return 1
        print("OK: no A answer for %s" % name)
        return 0

    if not answers:
        print("FAIL: no A answer for %s" % name)
        return 1
    if expected and not (set(answers) & expected):
        print("FAIL: answered %s, expected %s" %
              (", ".join(sorted(answers)), ", ".join(sorted(expected))))
        return 1
    print("OK: %s resolves to %s" % (name, ", ".join(sorted(answers))))
    return 0


if __name__ == "__main__":
    sys.exit(main())