"""Exercise production UTF-8 metadata, receiver state and version propagation."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from ota_metadata import encode_metadata, decode_metadata
from ota_package import make_package, unpack_package
from ota_protocol import APP_BASE, make_transfer


class MetadataTests(unittest.TestCase):
    def test_real_packages_and_user_version(self):
        for filename, profile, model in (("27_Touch.ota", 1, 0),
                ("99_Applications__991_AI_People_Detection.ota", 2, 1)):
            path = ROOT / "Binary/sdk_packages" / filename
            image, old_profile, old_desc = unpack_package(path.read_bytes())
            for version in ("1.2.3", ""):
                wire, crc = make_transfer(path.name, path.read_bytes(), "My note", version)
                actual, target, desc = unpack_package(wire)
                info = decode_metadata(desc)
                self.assertEqual((actual, target, crc), (image, profile, zlib.crc32(image)))
                self.assertEqual(info, {"name":path.stem,"model":model,
                                       "version":version,"description":"My note"})

    def test_old_description_and_embedded_metadata(self):
        self.assertIsNone(decode_metadata("991"))
        image = struct.pack("<II",0x34200000,APP_BASE+9)+b"APP"
        desc = encode_metadata("Camera", "2.0", 0, "note")
        package = make_package(image, 1, desc)
        payload, _ = make_transfer("renamed.ota", package, "updated")
        info = decode_metadata(unpack_package(payload)[2])
        self.assertEqual((info["name"],info["version"],info["description"]),
                         ("Camera","2.0","updated"))
        payload, _ = make_transfer("renamed.ota",package,"updated","")
        self.assertEqual(decode_metadata(unpack_package(payload)[2])["version"], "")

    def test_bounds_controls_and_utf8(self):
        text = encode_metadata("摄像头", "测试版本", 1, "人员检测")
        self.assertEqual(decode_metadata(text)["name"], "摄像头")
        for args in (("x"*128,"",0,""),("x","v"*64,0,""),
                     ("x","1\n2",0,""),("x","",3,""),("x","",0,"a\x00b")):
            with self.assertRaises(ValueError): encode_metadata(*args)
        for value in ("OTA-META1\n1\nname", "OTA-META1\n3\nn\nv\nd", "OTA-META1\n1\n\nv\nd"):
            with self.assertRaises(ValueError): decode_metadata(value)

    def test_production_receiver_status(self):
        folder = ROOT / "Build/ota-metadata-tests"
        folder.mkdir(parents=True,exist_ok=True)
        fixture = folder/"status_host.c"
        fixture.write_text(r'''
#include "ota_status.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 OtaStatus s;
 const char *m="OTA-META1\n1\n991 People Detection\n2.1.0\nmy note";
 ota_status_begin(); assert(ota_status_snapshot(&s));
 assert(!s.metadata_ready && !s.version[0] && s.model==2);
 assert(ota_status_package((const uint8_t *)m,strlen(m),2)==0);
 assert(ota_status_snapshot(&s) && s.metadata_ready);
 assert(!strcmp(s.name,"991 People Detection") && !strcmp(s.version,"2.1.0"));
 assert(s.model==1 && s.profile==2);
 ota_status_progress(OTA_UI_RECEIVING,1234,4065660);
 ota_status_progress(OTA_UI_WRITING,4096,4065660);
 assert(ota_status_snapshot(&s));
 assert(s.received==1234 && s.written==4096 && s.phase==OTA_UI_WRITING);
 ota_status_progress(OTA_UI_WRITING,99,10); assert(ota_status_snapshot(&s) && s.written==10);
 ota_status_phase(OTA_UI_ERROR,-15); assert(ota_status_snapshot(&s) && s.error==-15);
 ota_status_begin(); assert(ota_status_package((const uint8_t *)"legacy",6,1)==0);
 assert(ota_status_snapshot(&s) && !s.version[0] && s.model==2 && s.profile==1);
 assert(!strcmp(s.name,"legacy"));
 const char *bad="OTA-META1\n3\nname\nv\nd";
 assert(ota_status_package((const uint8_t *)bad,strlen(bad),1)<0);
 assert(ota_status_package((const uint8_t *)"abc",2001,1)<0);
 assert(ota_status_package((const uint8_t *)"abc",3,3)<0);
 puts("PASS production metadata parser, retries, bounds and phased progress");
 return 0;
}
''', encoding="ascii")
        compiler = shutil.which("gcc")
        self.assertIsNotNone(compiler)
        exe = folder/"status_host.exe"
        environment = dict(os.environ)
        environment["PATH"] = str(Path(compiler).parent)+os.pathsep+environment.get("PATH", "")
        subprocess.run([compiler,"-std=c11","-Wall","-Wextra","-Werror",
                        "-I"+str(ROOT/"Appli/Core/Inc"),str(fixture),
                        str(ROOT/"Appli/Core/Src/ota_status.c"),"-o",str(exe)],check=True,env=environment)
        subprocess.run([str(exe)],check=True,env=environment)


if __name__ == "__main__": unittest.main()
