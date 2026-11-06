#!/usr/bin/env python3
"""Host tests for tools/mkapp.py, tools/ihx2bin.py and tools/merge_ihx.py."""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TOOLS = os.path.join(ROOT, "tools")
sys.path.insert(0, TOOLS)

import mkapp  # noqa: E402


class HexTests(unittest.TestCase):
    def test_crc32_vector(self):
        self.assertEqual(mkapp.crc32(b"123456789"), 0xCBF43926)

    def test_round_trip(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "a.ihx")
            mem = {0x8000: 0x12, 0x8001: 0x34, 0x8005: 0xAB}
            mkapp.write_hex(path, mem)
            self.assertEqual(mkapp.read_hex(path), mem)

    def test_bad_checksum(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "bad.ihx")
            with open(path, "w") as file:
                file.write(":0100000011FF\n")
            with self.assertRaises(SystemExit):
                mkapp.read_hex(path)


class BuildTests(unittest.TestCase):
    def _image(self, tmp, boot_bytes, app_base=0x8A10, app_bytes=32):
        boot = os.path.join(tmp, "boot.ihx")
        app = os.path.join(tmp, "app-firmware.ihx")
        mkapp.write_hex(boot, {0x8000 + i: 0x55 for i in range(boot_bytes)})
        mkapp.write_hex(app, {app_base + i: i & 0xFF for i in range(app_bytes)})
        return boot, app

    def test_build_creates_app_and_factory(self):
        with tempfile.TemporaryDirectory() as tmp:
            boot, app = self._image(tmp, 512, app_bytes=32)
            out = os.path.join(tmp, "app.ihx")
            factory = os.path.join(tmp, "factory.ihx")
            raw = os.path.join(tmp, "app.bin")
            rc = mkapp.main(["--boot", boot, "--app", app, "--out", out,
                             "--factory", factory, "--bin", raw])
            self.assertEqual(rc, 0)

            app_mem = mkapp.read_hex(out)
            self.assertTrue(all(addr >= mkapp.HDR_BASE for addr in app_mem))
            self.assertEqual(bytes(app_mem[mkapp.HDR_BASE + i] for i in range(4)),
                             b"OKO8")
            length = int.from_bytes(bytes(app_mem[mkapp.HDR_BASE + 4 + i]
                                          for i in range(4)), "big")
            self.assertEqual(length, 32)
            self.assertIn(mkapp.APP_BASE, app_mem)

            factory_mem = mkapp.read_hex(factory)
            self.assertIn(0x8000, factory_mem)
            self.assertIn(mkapp.APP_BASE, factory_mem)

            with open(raw, "rb") as file:
                data = file.read()
            self.assertEqual(data[:4], b"OKO8")
            self.assertEqual(len(data), 16 + 32)

    def test_boot_overflow_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            boot, app = self._image(tmp, 0xA10)
            with self.assertRaises(SystemExit):
                mkapp.main(["--boot", boot, "--app", app,
                            "--out", os.path.join(tmp, "x.ihx")])

    def test_app_below_base_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            boot, app = self._image(tmp, 512)
            mkapp.write_hex(app, {0x8800: 1})
            with self.assertRaises(SystemExit):
                mkapp.main(["--boot", boot, "--app", app,
                            "--out", os.path.join(tmp, "x.ihx")])

    def test_bin_payload_matches_header_crc(self):
        with tempfile.TemporaryDirectory() as tmp:
            boot, app = self._image(tmp, 512, app_bytes=64)
            raw = os.path.join(tmp, "app.bin")
            mkapp.main(["--boot", boot, "--app", app,
                        "--out", os.path.join(tmp, "app.ihx"),
                        "--bin", raw])
            data = open(raw, "rb").read()
            # The header must describe exactly the payload that follows it.
            self.assertEqual(int.from_bytes(data[4:8], "big"), 64)
            self.assertEqual(int.from_bytes(data[12:16], "big"),
                             mkapp.crc32(data[16:]))

    def test_ihx2bin_and_merge(self):
        with tempfile.TemporaryDirectory() as tmp:
            one = os.path.join(tmp, "one.ihx")
            two = os.path.join(tmp, "two.ihx")
            merged = os.path.join(tmp, "merged.ihx")
            binary = os.path.join(tmp, "merged.bin")
            mkapp.write_hex(one, {0x8000: 1, 0x8001: 2})
            mkapp.write_hex(two, {0x8002: 3})
            subprocess.run([sys.executable, os.path.join(TOOLS, "merge_ihx.py"),
                            merged, one, two], check=True,
                           stdout=subprocess.DEVNULL)
            self.assertEqual(mkapp.read_hex(merged),
                             {0x8000: 1, 0x8001: 2, 0x8002: 3})
            subprocess.run([sys.executable, os.path.join(TOOLS, "ihx2bin.py"),
                            merged, binary, "--base", "0x8000"],
                           check=True, stdout=subprocess.DEVNULL)
            with open(binary, "rb") as file:
                self.assertEqual(file.read(), b"\x01\x02\x03")


if __name__ == "__main__":
    unittest.main(verbosity=2)