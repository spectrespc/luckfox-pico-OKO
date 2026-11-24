#!/usr/bin/env python3
"""I2C3 (STM8/EC bus) diagnostics on the OKO DUT.

KI-080 leaves `ff460000.i2c` (i2c3) dead after AOV cycles: the controller or a
slave holds SCL low.  This script reads the controller state that the vendor
`i2c-rk3x.c` does not expose, and offers the two recovery experiments that the
research note asked for (see research/rv1106-aov):

    dump      CON/CLKDIV/IEN/IPD/FCNT/SCL_OE_DB/ST/DBGCTRL/CON1 at the
              controller base, the pad levels via GPIO1_EXT_PORT, the i2c3
              interrupt count and `i2cdetect -r -y 3`
    --nak-release-scl {0,1}
              read-modify-write RKI2C_DBGCTRL[13] (TRM V0.3 Part1 p455):
              1 releases SCL high on NACK, 0 holds it low
    --bitbang 9 SCL clocks + STOP through GPIO1_D2/D3 after i2c3 is unbound
              (standard bus recovery); restores the I2C3_M1 iomux afterwards

Register map (TRM V0.3 Part1 §15.4, p444-455):
    0x000 CON, 0x004 CLKDIV, 0x018 IEN, 0x01c IPD, 0x020 FCNT,
    0x024 SCL_OE_DB, 0x220 RKI2C_ST (b1 scl_st, b0 sda_st),
    0x224 DBGCTRL (b14 h0_check_scl, b13 nak_release_scl, b12 flt_en),
    0x228 CON1
There is no STATUS register at 0x02c: the controller state register is ST at
0x220, and the pad levels come from GPIO1_EXT_PORT, not from the I2C block.

I2C3_M1 pins: GPIO1_D3 = SCL, GPIO1_D2 = SDA; iomux
GPIO1D_IOMUX_SEL_L = ioc + 0x18, value 0x3/0x3 for I2C3_M1, 0x0/0x0 for GPIO.
GPIO1 bank: base 32 (`/sys/class/gpio/gpiochip32`), DDR_H +0x0c, DR_H +0x04.

Examples:
    bench/run.sh aov_i2c_diag.py --dump
    bench/run.sh aov_i2c_diag.py --nak-release-scl 1 --dump
    bench/run.sh aov_i2c_diag.py --unbind --bitbang --rebind --dump
"""
import argparse
import json
import os
import re
import sys
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_ROOT = ROOT / "output" / "bench" / "i2c-diag"

HOST = os.environ.get("OKO_DUT_HOST", "192.168.52.33")
SSH_PASSWORD = os.environ.get("OKO_DUT_SSH_PASSWORD", "luckfox")

I2C_BUS = 3
I2C3_MASTER = "ff460000.i2c"
I2C3_BASE = 0xFF460000
IOC_GPIO1D_IOMUX = 0xFF538018
GPIO1_DR_H = 0xFF530004
GPIO1_DDR_H = 0xFF53000C
GPIO1_EXT_PORT = 0xFF530070
IOMUX_I2C3M1 = 0x3300                 # D3(SCL)=3, D2(SDA)=3
IOMUX_MASK = 0xFF00                   # D2/D3 = bits 8..15
GPIO1_D_BASE = 32                     # gpio1 chip base
SCL_GPIO = GPIO1_D_BASE + 27          # GPIO1_D3
SDA_GPIO = GPIO1_D_BASE + 26          # GPIO1_D2
SDA_BIT = 1 << 26                     # EXT_PORT bit of GPIO1_D2
SCL_BIT = 1 << 27                     # EXT_PORT bit of GPIO1_D3
SDA_DR = 1 << 10                      # DR_H/DDR_H bit of GPIO1_D2
SCL_DR = 1 << 11                      # DR_H/DDR_H bit of GPIO1_D3
DBGCTRL_NAK_RELEASE = 1 << 13

REGISTERS = [
    ("CON", 0x000), ("CLKDIV", 0x004), ("IEN", 0x018), ("IPD", 0x01C),
    ("FCNT", 0x020), ("SCL_OE_DB", 0x024), ("ST", 0x220),
    ("DBGCTRL", 0x224), ("CON1", 0x228),
]

CON_BITS = {"en": 0, "start": 3, "stop": 4, "lastack": 5, "actack": 6}
IPD_BITS = {"btf": 0, "brf": 1, "mbtf": 2, "mbrf": 3, "start": 4,
            "stop": 5, "nakrcv": 6, "slv_hdscl": 7}
CON1_BITS = {"auto_stop": 0, "auto_stop_tx_end": 1, "auto_stop_nak": 2}
DBGCTRL_BITS = {"flt_en": 12, "nak_release_scl": 13, "h0_check_scl": 14}

I2C_DEVICES = {0x40: "sht21", 0x41: "ina", 0x42: "ec", 0x60: "stm8"}
SCAN_RE = re.compile(r"^([0-9a-fA-F]{2}):(.*)$")


# --------------------------------------------------------------------------
# SSH helpers

def ssh_client(timeout=10, attempts=3):
    import paramiko
    last = None
    for attempt in range(1, attempts + 1):
        client = paramiko.SSHClient()
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        try:
            client.connect(HOST, username="root", password=SSH_PASSWORD,
                           timeout=max(timeout, 12),
                           banner_timeout=max(timeout, 20),
                           auth_timeout=max(timeout, 20))
            return client
        except Exception as exc:                            # noqa: BLE001
            last = exc
            try:
                client.close()
            except Exception:                               # noqa: BLE001
                pass
            if attempt < attempts:
                time.sleep(attempt)
    raise last if last is not None else RuntimeError("ssh failed")


def ssh_run(client, command, timeout=60):
    _stdin, stdout, stderr = client.exec_command(command, timeout=timeout)
    out = stdout.read().decode(errors="replace")
    err = stderr.read().decode(errors="replace")
    return stdout.channel.recv_exit_status(), out, err


def read32(client, address):
    code, out, _err = ssh_run(client, "devmem 0x%08x 32" % address)
    text = out.strip()
    if code != 0 or not text.startswith("0x"):
        return None
    return int(text, 16)


# Reading the block while pclk_i2c is auto-gated returns the APB bus keeper
# value (0x000512xx on this SoC) instead of the register.  Such reads are
# filtered and the read is repeated while a transfer keeps the clock alive.
JUNK_MASK = 0xFFFFFF00
JUNK_VALUE = 0x00051200
FIELD_LIMITS = {"CON": None, "CLKDIV": None, "IEN": 0xFFFF, "IPD": 0xFFFF,
                "FCNT": 0xFFFF, "SCL_OE_DB": 0xFFFF, "ST": 0x7,
                "DBGCTRL": 0x7FFF, "CON1": 0x7}
READ_ATTEMPTS = 12


def is_junk(value):
    return value is not None and (value & JUNK_MASK) == JUNK_VALUE


def plausible(name, value):
    if value is None:
        return False
    if name == "CLKDIV":
        return (value & 0xFFFF) == (value >> 16)
    limit = FIELD_LIMITS.get(name)
    return limit is None or value <= limit


def read_register(client, name, address, attempts=READ_ATTEMPTS):
    """Majority read with junk filtering; returns (value, raw_values)."""
    raw = []
    votes = {}
    for _attempt in range(attempts):
        value = read32(client, address)
        raw.append(value)
        if value is None or is_junk(value) or not plausible(name, value):
            continue
        votes[value] = votes.get(value, 0) + 1
        if votes[value] >= 2:
            return value, raw
    if votes:
        return max(votes, key=lambda item: (votes[item], -item)), raw
    return None, raw


def write32(client, address, value):
    code, out, err = ssh_run(client, "devmem 0x%08x 32 0x%08x" % (address, value))
    return code == 0, (out + err).strip()


def masked(address, mask, value):
    """HIWORD_UPDATE write: bits 31:16 carry the write mask for bits 15:0."""
    return (address, ((mask & 0xFFFF) << 16) | (value & mask))


def set_gpio_mux(client, value):
    """Mux GPIO1_D2/D3; only the fields in IOMUX_MASK are written."""
    _address, word = masked(IOC_GPIO1D_IOMUX, IOMUX_MASK, value)
    ok, detail = write32(client, IOC_GPIO1D_IOMUX, word)
    return ok, "%s (verify %s)" % (detail, read32(client, IOC_GPIO1D_IOMUX))


def unbind(client):
    return ssh_run(client, "echo %s > /sys/bus/platform/drivers/rk3x-i2c/"
                          "unbind && echo unbound" % I2C3_MASTER, timeout=30)


def rebind(client):
    return ssh_run(client, "echo %s > /sys/bus/platform/drivers/rk3x-i2c/"
                          "bind && echo bound" % I2C3_MASTER, timeout=30)


# --------------------------------------------------------------------------
# Snapshot

def _bits(value, table):
    if value is None:
        return {}
    return {name: 1 if value & (1 << bit) else 0 for name, bit in table.items()}


def parse_scan(text):
    found = []
    for line in text.splitlines():
        match = SCAN_RE.match(line.strip())
        if not match:
            continue
        base = int(match.group(1), 16)
        for offset, token in enumerate(match.group(2).split()):
            if token not in ("--", ""):
                found.append(base + offset)
    return sorted(found)


def snapshot(client, scan=True):
    """Read the controller, pads and interrupts; safe on a live or dead bus."""
    regs = {}
    junk = {}
    for name, offset in REGISTERS:
        value, raw = read_register(client, name, I2C3_BASE + offset)
        regs[name] = value
        junk[name] = sum(1 for item in raw if is_junk(item))
    pads = read32(client, GPIO1_EXT_PORT)
    iomux = read32(client, IOC_GPIO1D_IOMUX)
    ddr = read32(client, GPIO1_DDR_H)
    dr = read32(client, GPIO1_DR_H)
    _code, interrupts, _err = ssh_run(
        client, "grep ff460000 /proc/interrupts || true")
    _code, devices, _err = ssh_run(client, "ls /sys/bus/i2c/devices || true")
    scan_text = ""
    found = None
    if scan:
        _code, scan_text, _err = ssh_run(client, "i2cdetect -r -y %d" % I2C_BUS,
                                         timeout=40)
        found = parse_scan(scan_text)
    st = regs.get("ST")
    return {
        "regs": {"0x%03x" % offset: regs.get(name)
                 for name, offset in REGISTERS},
        "decoded": {
            "con": _bits(regs.get("CON"), CON_BITS),
            "con_mode": None if regs.get("CON") is None
            else (regs["CON"] >> 1) & 0x3,
            "ipd": _bits(regs.get("IPD"), IPD_BITS),
            "con1": _bits(regs.get("CON1"), CON1_BITS),
            "dbgctrl": _bits(regs.get("DBGCTRL"), DBGCTRL_BITS),
            "st_scl": None if st is None else st & 0x1,
            "st_sda": None if st is None else (st >> 1) & 0x1,
            "slv_hold_scl_th": None if regs.get("DBGCTRL") is None
            else (regs["DBGCTRL"] >> 8) & 0xF,
            "scl_oe_db": None if regs.get("SCL_OE_DB") is None
            else regs["SCL_OE_DB"] & 0xFF,
        },
        "pads": {
            "raw": pads,
            "sda": None if pads is None else (pads & SDA_BIT) and 1 or 0,
            "scl": None if pads is None else (pads & SCL_BIT) and 1 or 0,
            "iomux": iomux,
            "iomux_mode": iomux_mode(iomux),
            "iomux_scl": None if iomux is None else (iomux >> 12) & 0xF,
            "iomux_sda": None if iomux is None else (iomux >> 8) & 0xF,
            "gpio_ddr_h": ddr,
            "gpio_dr_h": dr,
        },
        "interrupts": interrupts.strip(),
        "i2c_devices": devices.split(),
        "junk_reads": junk,
        "scan": found,
        "scan_present": None if found is None else
        {("0x%02x" % address): I2C_DEVICES.get(address, "?") for address in found},
    }


def interpret(snap):
    """Short Russian verdict lines for the report."""
    lines = []
    pads = snap.get("pads") or {}
    dec = snap.get("decoded") or {}
    regs = snap.get("regs") or {}
    if pads.get("scl") == 0 and pads.get("sda") == 0:
        lines.append("state=3: SCL и SDA удержаны низко.")
    elif pads.get("scl") == 0:
        lines.append("state=2: SCL удержан низко (кандидат H2/H3).")
    elif pads.get("sda") == 0:
        lines.append("state=1: SDA удержан низко (slave-залипание).")
    elif pads.get("scl") == 1 and pads.get("sda") == 1:
        lines.append("Пады свободны: SCL=1, SDA=1.")
    ipd = regs.get("0x01c")
    if ipd is not None:
        if ipd == 0:
            lines.append("ipd=0x00: транзакция не отметилась (IOMUX/pull-up/"
                         "clock/периферия либо NACK без pending-бита).")
        if ipd & 0x10:
            lines.append("ipd=0x10: START pending — CPU не обслужил IRQ.")
        if ipd & 0x80:
            lines.append("ipd=0x80: SLV_HDSCL — slave удерживает SCL.")
        if ipd & 0x40:
            lines.append("ipd=0x40: NAKRCV — контроллер получил NACK.")
    if dec.get("con", {}).get("en"):
        lines.append("CON.EN=1: контроллер включён.")
    elif regs.get("0x000") is None:
        lines.append("CON не прочитан: блок в pclk-gate/reset или выключен.")
    else:
        lines.append("CON.EN=0: контроллер выключен/в reset.")
    if dec.get("con", {}).get("start") and not dec.get("con", {}).get("stop"):
        lines.append("CON.START висит без STOP: незавершённый START.")
    if dec.get("dbgctrl", {}).get("nak_release_scl") == 0:
        lines.append("DBGCTRL.nak_release_scl=0: после NACK SCL удерживается.")
    elif dec.get("dbgctrl", {}).get("nak_release_scl") == 1:
        lines.append("DBGCTRL.nak_release_scl=1: после NACK SCL отпускается.")
    if pads.get("iomux_mode") == "partial":
        lines.append("ВНИМАНИЕ: iomux сброшен частично — SCL(D3)=%s, "
                     "SDA(D2)=%s (3 = I2C3_M1, 0 = GPIO); пад управляется "
                     "GPIO-блоком." % (pads.get("iomux_scl"),
                                       pads.get("iomux_sda")))
    lines.append("I2C3_M1 iomux=%s (D3=%s, D2=%s)." % (
        pads.get("iomux_mode"), pads.get("iomux_scl"), pads.get("iomux_sda")))
    found = snap.get("scan")
    if found is not None:
        lines.append("i2cdetect: %s" % (
            ", ".join("0x%02x" % address for address in found) or "пусто"))
    return lines


def format_snapshot(snap, title):
    """Human-readable dump (Russian labels, values in hex)."""
    lines = ["# %s" % title, ""]
    lines.extend("Вывод: %s" % note for note in interpret(snap))
    lines.extend(["", "## Регистры контроллера (base 0xff460000)", "```"])
    for name, offset in REGISTERS:
        value = (snap.get("regs") or {}).get("0x%03x" % offset)
        lines.append("%-10s 0x%03x = %s" % (
            name, offset, "read-error" if value is None else "0x%08x" % value))
    lines.append("```")
    junk = snap.get("junk_reads") or {}
    unreadable = [name for name, value in (snap.get("regs") or {}).items()
                  if value is None]
    lines.extend(["", "## Читаемость", "```",
                  "junk-чтений (pclk закрыт): %s" % (
                      ", ".join("%s=%d" % (name, count)
                                for name, count in junk.items() if count)
                      or "нет"),
                  "не прочитаны: %s" % (", ".join(unreadable) or "нет"),
                  "```"])
    pads = snap.get("pads") or {}
    lines.extend(["", "## Пады и iomux", "```",
                  "GPIO1_EXT_PORT 0xff530070 = %s" % (
                      pads.get("raw") if pads.get("raw") is not None
                      else "read-error"),
                  "SDA(GPIO1_D2)=%s SCL(GPIO1_D3)=%s" % (
                      pads.get("sda"), pads.get("scl")),
                  "GPIO1D iomux 0xff538018 = %s (%s)" % (
                      pads.get("iomux"), pads.get("iomux_mode")),
                  "GPIO1 DDR_H=%s DR_H=%s" % (pads.get("gpio_ddr_h"),
                                              pads.get("gpio_dr_h")),
                  "```"])
    lines.extend(["", "## /proc/interrupts", "```",
                  snap.get("interrupts") or "(нет строки ff460000.i2c)", "```"])
    lines.extend(["", "## i2c devices и скан", "```",
                  " ".join(snap.get("i2c_devices") or []) or "(пусто)",
                  "found: %s" % (snap.get("scan_present") or "см. i2cdetect"),
                  "```"])
    return "\n".join(lines) + "\n"


# --------------------------------------------------------------------------
# Experiments

def sample_during_scan(client, samples=40, interval=0.05, scans=1):
    """Read the block while a scan keeps pclk_i2c enabled.

    `rk3x_i2c_xfer` prepares the clock only for the duration of a transfer;
    with the clock gated, /dev/mem reads of the block return bus junk.  A
    second SSH channel runs `i2cdetect`, the sampler reads the registers on
    the main channel, and distinct tuples are counted.
    """
    channel = None
    if scans > 0:
        channel = client.get_transport().open_session()
        channel.exec_command("for _n in " +
                             " ".join(str(n) for n in range(scans)) +
                             "; do i2cdetect -r -y %d >/dev/null 2>&1; done" %
                             I2C_BUS)
        time.sleep(0.05)
    rows = []
    for _ in range(samples):
        values = tuple(read_register(client, name, I2C3_BASE + offset,
                                     attempts=4)[0]
                       for name, offset in REGISTERS)
        rows.append(values)
        if interval:
            time.sleep(interval)
    if channel is not None:
        channel.close()
    counts = {}
    for values in rows:
        key = tuple("?" if value is None else "0x%08x" % value
                    for value in values)
        counts[key] = counts.get(key, 0) + 1
    return {"names": [name for name, _offset in REGISTERS],
            "samples": len(rows),
            "distinct": [{"values": dict(zip([name for name, _o in REGISTERS],
                                            key)),
                          "count": count}
                         for key, count in sorted(counts.items(),
                                                  key=lambda item: -item[1])]}


def iomux_mode(iomux):
    """Classify the GPIO1_D iomux word: both pins I2C3_M1, one, or none."""
    if iomux is None:
        return None
    field = iomux & IOMUX_MASK
    if field == IOMUX_I2C3M1:
        return "i2c3m1"
    return "partial" if field else "gpio"


def quick_pads(client=None):
    """One round trip: pad levels and the GPIO1_D iomux word."""
    own = client is None
    if own:
        client = ssh_client(timeout=5, attempts=1)
    try:
        pads = read32(client, GPIO1_EXT_PORT)
        iomux = read32(client, IOC_GPIO1D_IOMUX)
    finally:
        if own:
            client.close()
    return {"raw": pads,
            "sda": None if pads is None else (pads & SDA_BIT) and 1 or 0,
            "scl": None if pads is None else (pads & SCL_BIT) and 1 or 0,
            "iomux": iomux,
            "iomux_mode": iomux_mode(iomux),
            "iomux_scl": None if iomux is None else (iomux >> 12) & 0xF,
            "iomux_sda": None if iomux is None else (iomux >> 8) & 0xF}


def heal_loop(seconds, interval=5.0, scan=True):
    """Watch the pads and restore the I2C3_M1 mux whenever it is lost."""
    client = ssh_client()
    deadline = time.time() + seconds
    rows = []
    try:
        while time.time() < deadline:
            try:
                pads = quick_pads(client)
            except Exception as exc:                        # noqa: BLE001
                rows.append({"ts": time.time(), "error": str(exc)})
                try:
                    client.close()
                except Exception:                           # noqa: BLE001
                    pass
                time.sleep(min(interval, 2.0))
                try:
                    client = ssh_client(timeout=5, attempts=1)
                except Exception:                           # noqa: BLE001
                    client = None
                continue
            row = {"ts": time.time(), **_pads_summary(pads)}
            if pads.get("iomux_mode") != "i2c3m1" or pads.get("scl") == 0:
                ok, detail = restore_iomux(client)
                row.update({"healed": ok, "detail": detail})
                if scan:
                    _code, out, _err = ssh_run(
                        client, "i2cdetect -r -y %d" % I2C_BUS, timeout=40)
                    row["scan"] = ["0x%02x" % address
                                   for address in parse_scan(out)]
            rows.append(row)
            time.sleep(interval)
    finally:
        if client is not None:
            try:
                client.close()
            except Exception:                               # noqa: BLE001
                pass
    healed = [row for row in rows if row.get("healed")]
    return {"seconds": seconds, "polls": len(rows), "healed": len(healed),
            "rows": rows}


def _pads_summary(pads):
    return {"pads": pads.get("raw"), "scl": pads.get("scl"),
            "sda": pads.get("sda"), "iomux": pads.get("iomux"),
            "iomux_mode": pads.get("iomux_mode"),
            "iomux_scl": pads.get("iomux_scl"),
            "iomux_sda": pads.get("iomux_sda")}


def set_nak_release_scl(client, enable):
    """RMW RKI2C_DBGCTRL[13]; returns (ok, detail)."""
    current = read32(client, I2C3_BASE + 0x224)
    if current is None:
        return False, "DBGCTRL read failed"
    target = (current | DBGCTRL_NAK_RELEASE) if enable else \
        (current & ~DBGCTRL_NAK_RELEASE)
    ok, detail = write32(client, I2C3_BASE + 0x224, target)
    verify = read32(client, I2C3_BASE + 0x224)
    return ok and verify == target, "DBGCTRL 0x%08x -> 0x%08x (verify %s)" % (
        current, target, "0x%08x" % verify if verify is not None else "fail")


def bitbang(client, clocks=9, half_us=1000):
    """9 SCL clocks + STOP over GPIO1_D2/D3; i2c3 must be unbound.

    Rockchip IOC and GPIO data registers take HIWORD_UPDATE words, so every
    write carries its bit mask in bits 31:16; a plain value is ignored.  The
    pads keep their external pull-ups: SDA is driven only for the STOP
    condition, SCL is driven for the clocks and released afterwards.
    """
    def op(address, mask_mask, value):
        _addr, word = masked(address, mask_mask, value)
        return "devmem 0x%08x 32 0x%08x" % (address, word)

    lines = [op(IOC_GPIO1D_IOMUX, IOMUX_MASK, 0x0),
             op(GPIO1_DDR_H, SCL_DR, SCL_DR),
             op(GPIO1_DR_H, SCL_DR, SCL_DR),
             op(GPIO1_DDR_H, SDA_DR, 0),
             "echo \"PADS-START $(devmem 0x%08x 32)\"" % GPIO1_EXT_PORT]
    for index in range(1, clocks + 1):
        lines.append(op(GPIO1_DR_H, SCL_DR, 0))
        lines.append("usleep %d" % half_us)
        lines.append("echo \"CLK %d LOW $(devmem 0x%08x 32)\"" %
                     (index, GPIO1_EXT_PORT))
        lines.append(op(GPIO1_DR_H, SCL_DR, SCL_DR))
        lines.append("usleep %d" % half_us)
        lines.append("echo \"CLK %d HIGH $(devmem 0x%08x 32)\"" %
                     (index, GPIO1_EXT_PORT))
    lines.extend([
        op(GPIO1_DDR_H, SDA_DR, SDA_DR),
        op(GPIO1_DR_H, SDA_DR, 0),
        "usleep %d" % half_us,
        op(GPIO1_DR_H, SDA_DR, SDA_DR),
        "usleep %d" % half_us,
        op(GPIO1_DDR_H, SDA_DR, 0),
        "usleep %d" % half_us,
        "echo \"IOMUX-END $(devmem 0x%08x 32)\"" % IOC_GPIO1D_IOMUX,
        "echo \"PADS-END $(devmem 0x%08x 32)\"" % GPIO1_EXT_PORT,
    ])
    code, out, err = ssh_run(client, "\n".join(lines), timeout=120)
    return {"code": code, "output": out + err,
            "clocks": [line for line in out.splitlines()
                       if line.startswith("CLK ")]}


def restore_iomux(client):
    """Put GPIO1_D2/D3 back to I2C3_M1 and release the GPIO drivers."""
    ok_mux, detail = set_gpio_mux(client, IOMUX_I2C3M1)
    # Boot leaves D3 as a GPIO output driving low; restore that so a later
    # partial iomux reset reproduces the field failure instead of masking it.
    _addr, ddr = masked(GPIO1_DDR_H, SCL_DR | SDA_DR, SCL_DR)
    write32(client, GPIO1_DDR_H, ddr)
    _addr, dr = masked(GPIO1_DR_H, SCL_DR | SDA_DR, 0)
    write32(client, GPIO1_DR_H, dr)
    return ok_mux, detail


def mux_loss(client, scl_field=0):
    """Reproduce KI-080/H2: drop the SCL iomux field while the pad drives low."""
    _addr, ddr = masked(GPIO1_DDR_H, SCL_DR, SCL_DR)
    write32(client, GPIO1_DDR_H, ddr)
    _addr, dr = masked(GPIO1_DR_H, SCL_DR, 0)
    write32(client, GPIO1_DR_H, dr)
    ok, detail = set_gpio_mux(
        client, (IOMUX_I2C3M1 & ~(0xF << 12)) | ((scl_field & 0xF) << 12))
    return ok, detail


# --------------------------------------------------------------------------
# CLI

def main():
    parser = argparse.ArgumentParser(
        description="I2C3 (STM8/EC) register and pad diagnostics")
    parser.add_argument("--dump", action="store_true",
                        help="read registers/pads/scan (default action)")
    parser.add_argument("--nak-release-scl", type=int, choices=[0, 1],
                        default=None,
                        help="write RKI2C_DBGCTRL[13] before the dump")
    parser.add_argument("--unbind", action="store_true",
                        help="unbind rk3x-i2c for ff460000.i2c first")
    parser.add_argument("--rebind", action="store_true",
                        help="bind rk3x-i2c for ff460000.i2c at the end")
    parser.add_argument("--bitbang", action="store_true",
                        help="9 SCL clocks + STOP over GPIO1_D2/D3")
    parser.add_argument("--mux-loss", action="store_true",
                        help="reproduce H2: SCL iomux -> GPIO with the pad low")
    parser.add_argument("--restore-mux", action="store_true",
                        help="write I2C3_M1 back into the GPIO1_D iomux")
    parser.add_argument("--no-scan", action="store_true",
                        help="skip i2cdetect (it floods dmesg)")
    parser.add_argument("--sample", type=int, default=0,
                        help="read registers N times while i2cdetect runs")
    parser.add_argument("--sample-scans", type=int, default=1,
                        help="scans to run during --sample (0: no traffic)")
    parser.add_argument("--heal", type=float, default=0,
                        help="watch and re-apply the iomux for N seconds")
    parser.add_argument("--heal-interval", type=float, default=5.0)
    parser.add_argument("--no-heal-scan", dest="heal_scan",
                        action="store_false",
                        help="do not scan the bus after a mux repair")
    parser.add_argument("--out-dir", default=None)
    parser.add_argument("--prefix", default="i2c-diag")
    args = parser.parse_args()
    if not (args.dump or args.bitbang or args.nak_release_scl is not None or
            args.unbind or args.rebind or args.heal or args.mux_loss or
            args.restore_mux):
        args.dump = True

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    outdir = Path(args.out_dir) if args.out_dir else OUT_ROOT / stamp
    outdir.mkdir(parents=True, exist_ok=True)
    report = {"host": HOST, "stamp": stamp, "actions": []}

    client = ssh_client()
    try:
        report["before"] = snapshot(client, scan=not args.no_scan)
        if args.heal:
            report["heal"] = heal_loop(args.heal, args.heal_interval,
                                       scan=args.heal_scan)
        if args.sample:
            report["sampled"] = sample_during_scan(
                client, samples=args.sample, scans=args.sample_scans)
        if args.unbind:
            code, out, err = unbind(client)
            report["actions"].append({"unbind": code, "detail": (out + err).strip()})
            time.sleep(1)
            report["after_unbind"] = snapshot(client, scan=False)
        if args.mux_loss:
            ok, detail = mux_loss(client)
            report["actions"].append({"mux_loss": ok, "detail": detail})
            time.sleep(0.2)
            report["after_mux_loss"] = snapshot(client, scan=not args.no_scan)
            _code, dmesg, _err = ssh_run(client, "dmesg | tail -20")
            report["mux_loss_dmesg"] = dmesg
        if args.restore_mux:
            ok, detail = restore_iomux(client)
            report["actions"].append({"restore_mux": ok, "detail": detail})
            time.sleep(2)
            report["after_restore_mux"] = snapshot(client,
                                                   scan=not args.no_scan)
        if args.bitbang:
            report["bitbang"] = bitbang(client)
            time.sleep(0.2)
            report["after_bitbang"] = snapshot(client, scan=not args.no_scan)
        if args.nak_release_scl is not None:
            ok, detail = set_nak_release_scl(client, bool(args.nak_release_scl))
            report["actions"].append({"nak_release_scl":
                                      args.nak_release_scl, "ok": ok,
                                      "detail": detail})
            scan_text = ""
            if not args.no_scan:
                _code, scan_text, _err = ssh_run(
                    client, "i2cdetect -r -y %d" % I2C_BUS, timeout=40)
            report["after_nak"] = {
                "pads": read32(client, GPIO1_EXT_PORT),
                "scan": parse_scan(scan_text),
            }
        if args.rebind:
            restore_iomux(client)
            code, out, err = rebind(client)
            report["actions"].append({"rebind": code, "detail": (out + err).strip()})
            time.sleep(2)
            report["after_rebind"] = snapshot(client, scan=not args.no_scan)
    finally:
        client.close()

    (outdir / ("%s.json" % args.prefix)).write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n")
    text = format_snapshot(report["before"], "I2C3 до вмешательства")
    if "sampled" in report:
        text += "\n## Регистры во время скана (pclk включён)\n```\n%s\n```\n" % \
            json.dumps(report["sampled"], indent=2, ensure_ascii=False)
    for key in ("after_unbind", "after_mux_loss", "after_restore_mux",
                "after_bitbang", "after_rebind"):
        if key in report:
            text += "\n" + format_snapshot(report[key], key)
    if "mux_loss_dmesg" in report:
        text += "\n## dmesg после имитации H2\n```\n%s\n```\n" % \
            (report["mux_loss_dmesg"] or "").strip()
    if "heal" in report:
        heal = report["heal"]
        text += "\n## Наблюдение и восстановление iomux\n" \
                "опросов: %d, восстановлений: %d\n```\n" % (
                    heal["polls"], heal["healed"])
        for row in heal["rows"]:
            if "error" in row:
                text += "error: %s\n" % row["error"]
                continue
            text += ("scl=%s sda=%s iomux=%s(%s D3=%s D2=%s)%s\n" % (
                row.get("scl"), row.get("sda"), row.get("iomux"),
                row.get("iomux_mode"), row.get("iomux_scl"),
                row.get("iomux_sda"),
                " -> HEALED scan=%s" % row.get("scan")
                if row.get("healed") else ""))
        text += "```\n"
    if "bitbang" in report:
        text += "\n## Bit-bang\n```\n%s\n```\n" % \
            (report["bitbang"].get("output") or "").strip()
    if "after_nak" in report:
        text += "\n## nak_release_scl\n```\n%s\n```\n" % json.dumps(
            report["after_nak"], ensure_ascii=False)
    (outdir / ("%s.txt" % args.prefix)).write_text(text)
    sys.stdout.write(text)
    print("artifact dir: %s" % outdir)
    return 0


if __name__ == "__main__":
    sys.exit(main())