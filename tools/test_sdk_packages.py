"""Verify SDK image packing and all generated application/model pairs."""
from pathlib import Path
import json
import base64
import http.client
import struct
import sys
import threading
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from ota_package import APP_BASE, ACTIVE_END, PACKAGE_MAX_IMAGE, hex_segments, merge_sdk_images, make_package, unpack_package
from ota_protocol import make_transfer


def ihex(addr, data):
    def record(kind, offset, payload):
        r = bytes([len(payload)]) + offset.to_bytes(2, "big") + bytes([kind]) + payload
        return ":" + (r + bytes([-sum(r) & 255])).hex().upper()
    return "\n".join([record(4, 0, (addr >> 16).to_bytes(2, "big")),
                      record(0, addr & 65535, data), record(1, 0, b"")])


class PackageTests(unittest.TestCase):
    def test_absolute_resources_and_sparse_padding(self):
        app = struct.pack("<II", 0x34200000, APP_BASE + 9) + b"APP"
        weights = b"MODEL123"
        image, segments = merge_sdk_images(ihex(APP_BASE, app), ihex(0x70200000, weights))
        self.assertEqual(image[:len(app)], app)
        self.assertEqual(image[0xFFC00:], weights)
        self.assertEqual(image[len(app):0xFFC00], b"\xff" * (0xFFC00 - len(app)))
        self.assertEqual(len(segments), 2)
        package = make_package(image, 2, "AI")
        self.assertEqual(unpack_package(package), (image, 2, "AI"))
        renamed, crc = make_transfer("demo.ota", package, "new description")
        self.assertEqual(unpack_package(renamed), (image, 2, "new description"))
        self.assertEqual(crc, zlib.crc32(image))

    def test_reject_loader_agent_overlap_and_out_of_range_models(self):
        app = struct.pack("<II", 0x34200000, APP_BASE + 9) + b"APP"
        for addr in (0x70000000, 0x71F00400):
            with self.assertRaises(ValueError):
                merge_sdk_images(ihex(addr, app))
        for addr in (0x701FFFFF, ACTIVE_END):
            with self.assertRaises(ValueError):
                merge_sdk_images(ihex(APP_BASE, app), ihex(addr, b"MODEL"))
        duplicate = ihex(APP_BASE, app).splitlines()
        with self.assertRaises(ValueError):
            hex_segments("\n".join(duplicate[:-1] + duplicate[1:]))

    def test_package_corruption_truncation_and_profile(self):
        app = struct.pack("<II", 0x34200000, APP_BASE + 9) + b"APP"
        p = make_package(app, 1, "LED")
        for invalid in (p[:-1], p + b"x", p[:-1] + bytes([p[-1] ^ 1])):
            with self.assertRaises(ValueError):
                unpack_package(invalid)
        with self.assertRaises(ValueError):
            make_package(app, 3, "bad")
        with self.assertRaises(ValueError):
            make_package(app + b"\xff" * PACKAGE_MAX_IMAGE, 2, "large")

    def test_generated_catalog_matches_every_original_segment(self):
        inventory = json.loads((ROOT / "Binary/sdk_packages/inventory.json").read_text(encoding="utf-8"))
        self.assertFalse(inventory["failed"])
        self.assertEqual(len(inventory["prepared"]), 77)
        ai = 0
        for entry in inventory["prepared"]:
            image, profile, _ = unpack_package(Path(entry["package"]).read_bytes())
            self.assertEqual(profile, entry["profile"])
            if profile == 2:
                ai += 1
                self.assertEqual(len(entry["sources"]), 2)
            for src in entry["sources"]:
                for addr, data in hex_segments(Path(src["path"]).read_text(encoding="ascii")):
                    self.assertEqual(image[addr - APP_BASE:addr - APP_BASE + len(data)], data)
        self.assertEqual(ai, 5)

    def test_web_upload_preserves_package_profile(self):
        import ota_gui
        from http.server import ThreadingHTTPServer
        app = struct.pack("<II", 0x34200000, APP_BASE + 9) + b"APP"
        package = make_package(app, 1, "SDK")
        completed = threading.Event()
        received = []
        def worker(filename, desc, payload, crc, rollback=False):
            received.append((unpack_package(payload), crc, rollback))
            completed.set()
        with patch("ota_gui._send_worker", worker):
            with ota_gui._lock:
                ota_gui._state["phase"] = "idle"
            server = ThreadingHTTPServer(("127.0.0.1", 0), ota_gui.Handler)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                client = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=5)
                body = json.dumps({"file": "SDK.ota", "desc": "LED", "data": base64.b64encode(package).decode()})
                client.request("POST", "/api/send", body, {"Content-Type": "application/json"})
                self.assertTrue(json.loads(client.getresponse().read())["ok"])
                self.assertTrue(completed.wait(5))
                from ota_metadata import decode_metadata
                actual, crc, rollback = received[0]
                self.assertEqual(actual[:2], (app, 1))
                self.assertEqual((crc, rollback), (zlib.crc32(app), False))
                self.assertEqual(decode_metadata(actual[2]),
                                 {"name":"SDK", "version":"", "model":0, "description":"LED"})
                client.close()
            finally:
                server.shutdown(); server.server_close(); thread.join(5)


if __name__ == "__main__":
    unittest.main()
