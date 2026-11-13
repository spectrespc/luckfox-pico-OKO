#!/usr/bin/env python3
"""UART transport for the DUT console.

Two backends share one pyserial-like interface (`read`, `write`,
`reset_input_buffer`, `close`, `name`):

* a local USB-UART adapter (`/dev/ttyACM*`), handled by pyserial;
* the ESP32 telnet-UART bridge, handled by a small telnet client: the
  connection is a raw TCP stream with the usual IAC negotiation at the
  start, which is answered and stripped here.

The target defaults to `telnet://192.168.52.17:23` and can be overridden
with `OKO_SERIAL_PORT` or the per-script `--port` option.  A value with a
`telnet://` prefix or a `host:port` shape selects the telnet backend;
anything else is treated as a serial device.
"""
import os
import re
import socket

DEFAULT_TARGET = "telnet://192.168.52.17:23"
DEFAULT_BAUDRATE = 115200

_IAC = 0xFF
_DONT, _DO, _WONT, _WILL = 0xFE, 0xFD, 0xFC, 0xFB
_SB, _SE = 0xFA, 0xF0

_TELNET_RE = re.compile(r"^(?:telnet://)?([A-Za-z0-9_.-]+):(\d+)$")


def default_target():
    """Return OKO_SERIAL_PORT or the bench telnet bridge."""
    return os.environ.get("OKO_SERIAL_PORT") or DEFAULT_TARGET


def is_telnet(spec):
    return bool(spec) and bool(_TELNET_RE.match(spec))


def _parse_telnet(spec):
    match = _TELNET_RE.match(spec)
    return match.group(1), int(match.group(2))


class TelnetTransport:
    """TCP transport for the ESP32 telnet-UART bridge.

    The bridge announces IAC WILL ECHO / WILL SGA on connect.  The DUT tty
    already echoes what is typed, so every option is refused (DONT/WONT) and
    the IAC bytes are removed from the stream; otherwise login prompts and
    echoed commands would be interleaved with negotiation bytes.
    """

    def __init__(self, host, port, timeout=0.2):
        self.name = f"telnet://{host}:{port}"
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.sock.settimeout(timeout)
        self._pending = b""

    def _refuse(self, cmd, option):
        if cmd == _WILL:
            reply = bytes((_IAC, _DONT, option))
        elif cmd == _DO:
            reply = bytes((_IAC, _WONT, option))
        else:
            return
        try:
            self.sock.sendall(reply)
        except OSError:
            pass

    def _decode(self, data):
        """Strip telnet commands from data, keeping incomplete tails buffered."""
        data = self._pending + data
        out = bytearray()
        index = 0
        size = len(data)
        while index < size:
            byte = data[index]
            if byte != _IAC:
                out.append(byte)
                index += 1
                continue
            if index + 1 >= size:
                break
            cmd = data[index + 1]
            if cmd == _IAC:
                out.append(_IAC)
                index += 2
            elif cmd in (_WILL, _WONT, _DO, _DONT):
                if index + 2 >= size:
                    break
                self._refuse(cmd, data[index + 2])
                index += 3
            elif cmd == _SB:
                end = data.find(bytes((_IAC, _SE)), index + 2)
                if end < 0:
                    break
                index = end + 2
            else:
                index += 2
        self._pending = data[index:]
        return bytes(out)

    def read(self, size=4096):
        try:
            data = self.sock.recv(size)
        except OSError:
            return b""
        if not data:
            return b""
        return self._decode(data)

    def write(self, data):
        if isinstance(data, str):
            data = data.encode()
        self.sock.sendall(data)
        return len(data)

    def reset_input_buffer(self):
        timeout = self.sock.gettimeout()
        self.sock.settimeout(0)
        try:
            while self.sock.recv(4096):
                pass
        except OSError:
            pass
        finally:
            self.sock.settimeout(timeout)
        self._pending = b""

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class SerialTransport:
    """pyserial device with the telnet transport's attribute set."""

    def __init__(self, device, baudrate=DEFAULT_BAUDRATE, timeout=0.2):
        import serial

        self.name = device
        self.serial = serial.Serial(device, baudrate, timeout=timeout)

    def read(self, size=4096):
        return self.serial.read(size)

    def write(self, data):
        return self.serial.write(data)

    def reset_input_buffer(self):
        self.serial.reset_input_buffer()

    def close(self):
        self.serial.close()


def open_uart(spec=None, baudrate=DEFAULT_BAUDRATE, timeout=0.2):
    """Open the DUT console: telnet bridge or a local serial adapter."""
    spec = spec or default_target()
    if is_telnet(spec):
        host, port = _parse_telnet(spec)
        return TelnetTransport(host, port, timeout=timeout)
    return SerialTransport(spec, baudrate=baudrate, timeout=timeout)
