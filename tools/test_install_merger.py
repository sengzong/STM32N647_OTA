"""Exercise the actual portable EXE against independent Python HEX/CRC readers."""
from pathlib import Path
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from ota_package import hex_segments
from migrate_ota_layout import write_hex

FOLDER = ROOT / "Build/install-merger-tests"
COMPAT = ROOT / "Binary/compat"
EXE = ROOT / "Portable/InitialInstallMerger/InitialInstallMerger.exe"


def read_hex(path):
    return hex_segments(path.read_text(encoding="ascii"))


def read_range(parts, address, size):
    result = bytearray()
    while size:
        for start, data in parts:
            if start <= address < start + len(data):
                count = min(size, start + len(data) - address)
                result.extend(data[address-start:address-start+count])
                size -= count
                address += count
                break
        else:
            raise AssertionError(f"Missing byte at {address:#x}")
    return bytes(result)


def app_image():
    image = bytearray((index*7) & 255 for index in range(64))
    struct.pack_into("<II", image, 0, 0x34200000, 0x70100421)
    return bytes(image)


class MergerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        FOLDER.mkdir(parents=True, exist_ok=True)
        if not EXE.is_file():
            raise RuntimeError("Run tools/build_install_merger.ps1 first")
        cls.app = FOLDER/"small_app.hex"
        write_hex([(0x70100400, app_image())], cls.app)

    def run_tool(self, output, *, fsbl=None, agent=None, app=None, model=None,
                 profile="default", overwrite=False, success=True, exe=EXE, env=None):
        arguments = [str(exe), "--fsbl", str(fsbl or COMPAT/"fsbl_universal.hex"),
                     "--agent", str(agent or COMPAT/"agent.hex"),
                     "--app", str(app or COMPAT/"appli.hex"),
                     "--profile", profile, "--out", str(output)]
        if model:
            arguments += ["--model", str(model)]
        if overwrite:
            arguments += ["--overwrite"]
        result = subprocess.run(arguments, capture_output=True, timeout=30, env=env)
        self.assertEqual(result.returncode, 0 if success else 2,
                         result.stdout.decode("utf-8", errors="replace") + result.stderr.decode("utf-8", errors="replace"))
        return result

    def fresh_output(self, name):
        path = FOLDER/name
        path.unlink(missing_ok=True)
        return path

    def test_current_firmware_matches_published_installer_exactly(self):
        output = self.fresh_output("current.hex")
        self.run_tool(output)
        self.assertEqual(output.read_bytes(), (COMPAT/"initial_install.hex").read_bytes())
        parts = read_hex(output)
        for name in ("fsbl_universal.hex", "agent.hex", "appli.hex"):
            for address, data in read_hex(COMPAT/name):
                self.assertEqual(read_range(parts, address, len(data)), data)
        for address in (0x700FC000, 0x70B00000, 0x71500000):
            self.assertEqual(read_range(parts, address, 4096), b"\xff"*4096)

    def test_600_clock_runtime_records_and_default_profile(self):
        for profile, value in (("sdk600", 1), ("ai800", 2), ("default", 0)):
            with self.subTest(profile=profile):
                output = self.fresh_output(profile+".hex")
                self.run_tool(output, app=self.app, profile=profile)
                parts = read_hex(output)
                header = read_range(parts, 0x70100000, 32)
                self.assertEqual(struct.unpack_from("<4I", header),
                                 (0x3254524F, value, len(app_image()), zlib.crc32(app_image())))
                self.assertEqual(struct.unpack_from("<I", header, 28)[0], zlib.crc32(header[:28]))

    def test_paired_model_fills_gap_and_updates_total_image_crc(self):
        model = FOLDER/"network-data.hex"
        payload = bytes(range(128))
        write_hex([(0x70200000, payload)], model)
        output = self.fresh_output("with-model.hex")
        self.run_tool(output, app=self.app, model=model, profile="ai800")
        parts = read_hex(output)
        expected = app_image() + b"\xff"*(0x70200000 - 0x70100400 - len(app_image())) + payload
        self.assertEqual(read_range(parts, 0x70100400, len(expected)), expected)
        self.assertEqual(struct.unpack("<4I", read_range(parts, 0x70100000, 16)),
                         (0x3254524F, 2, len(expected), zlib.crc32(expected)))
        bad = self.fresh_output("bad-model-clock.hex")
        self.run_tool(bad, app=self.app, model=model, profile="sdk600", success=False)
        self.assertFalse(bad.exists())

    def test_rejects_old_agent_and_combined_hex_as_application(self):
        output = self.fresh_output("wrong-components.hex")
        self.run_tool(output, agent=ROOT/"Binary/agent.hex", success=False)
        self.assertFalse(output.exists())
        for name in ("initial_install.hex", "management_update.hex"):
            self.run_tool(output, app=COMPAT/name, success=False)
            self.assertFalse(output.exists())

    def test_corrupt_fsbl_headers_are_rejected_without_overwriting_output(self):
        original = read_hex(COMPAT/"fsbl_universal.hex")[0][1]
        output = FOLDER/"keep-existing.hex"
        for kind in ("checksum", "entry", "version", "length", "alignment"):
            with self.subTest(kind=kind):
                raw = bytearray(original)
                if kind == "checksum": raw[-1] ^= 1
                elif kind == "entry": raw[0x70] ^= 4
                elif kind == "version": raw[0x68] ^= 1
                elif kind == "length": raw[0x6C] ^= 1
                else: raw = raw[:0x240] + raw[0x400:]
                path = FOLDER/(kind+"_fsbl.hex")
                write_hex([(0x70000000, bytes(raw))], path)
                output.write_bytes(b"preserve this output")
                self.run_tool(output, fsbl=path, overwrite=True, success=False)
                self.assertEqual(output.read_bytes(), b"preserve this output")

    def test_hex_corruption_overlap_and_bad_vector_rejected(self):
        output = self.fresh_output("bad.hex")
        for kind in ("overlap", "missing-eof", "checksum", "vector", "out-of-range"):
            with self.subTest(kind=kind):
                path = FOLDER/(kind+"_app.hex")
                if kind == "overlap":
                    write_hex([(0x70100400, app_image()), (0x70100400, b"1234")], path)
                elif kind == "vector":
                    write_hex([(0x70100400, bytes(64))], path)
                elif kind == "out-of-range":
                    write_hex([(0x70100400, app_image()), (0x70AFF000, b"x")], path)
                else:
                    text = self.app.read_text(encoding="ascii")
                    if kind == "missing-eof": text = text[:text.rfind(":00000001FF")]
                    else: text = text.replace("7010", "7011", 1)
                    path.write_text(text, encoding="ascii")
                self.run_tool(output, app=path, success=False)
                self.assertFalse(output.exists())

    def test_output_cannot_overwrite_inputs_and_requires_explicit_overwrite(self):
        output = FOLDER/"existing.hex"
        output.write_bytes(b"previous")
        self.run_tool(output, success=False)
        self.assertEqual(output.read_bytes(), b"previous")
        self.run_tool(output, overwrite=True)
        self.assertEqual(output.read_bytes(), (COMPAT/"initial_install.hex").read_bytes())
        previous = self.app.read_bytes()
        self.run_tool(self.app, app=self.app, overwrite=True, success=False)
        self.assertEqual(self.app.read_bytes(), previous)
        self.assertFalse(list(FOLDER.glob("*.tmp-*")))

    def test_model_range_and_app_reset_into_model_are_rejected(self):
        output = self.fresh_output("invalid-pair.hex")
        model = FOLDER/"bad-model.hex"
        write_hex([(0x701F0000, bytes(32))], model)
        self.run_tool(output, app=self.app, model=model, profile="ai800", success=False)
        write_hex([(0x70200000, bytes(32))], model)
        image = bytearray(app_image())
        struct.pack_into("<I", image, 4, 0x70200001)
        app = FOLDER/"bad-vector-app.hex"
        write_hex([(0x70100400, bytes(image))], app)
        self.run_tool(output, app=app, model=model, profile="ai800", success=False)
        self.assertFalse(output.exists())

    def test_detached_unicode_space_folder_needs_no_python_in_path(self):
        detached = FOLDER/"独立副本 工具"
        detached.mkdir(exist_ok=True)
        program = detached/EXE.name
        shutil.copyfile(EXE, program)
        environment = dict(os.environ)
        environment["PATH"] = str(Path(os.environ["WINDIR"])/"System32")
        output = self.fresh_output("detached.hex")
        self.run_tool(output, exe=program, env=environment)
        self.assertEqual(hashlib.sha256(output.read_bytes()).digest(),
                         hashlib.sha256((COMPAT/"initial_install.hex").read_bytes()).digest())

    def test_native_gui_can_render_without_visible_window(self):
        path = self.fresh_output("preview.png")
        result = subprocess.run([str(EXE), "--preview", str(path)], capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(path.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))
        from PIL import Image
        with Image.open(path) as image:
            self.assertGreaterEqual(image.width, 950)
            self.assertGreaterEqual(image.height, 760)
            pixels = list(image.convert("RGB").getdata())
            self.assertGreater(sum(max(p)-min(p)>40 and p[1]>80 for p in pixels), 8000)
            self.assertGreater(sum(max(p)<100 for p in pixels), 2000)


if __name__ == "__main__":
    unittest.main()
