"""Build all three OTA images from this checkout, without stale IDE makefiles."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
from verify_fsbl_image import verify_fsbl_image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
OUT = ROOT / "Build/ota"


def run(command, log=None):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True,
                            encoding="utf-8", errors="replace", input="", timeout=120)
    if log:
        Path(log).write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(" ".join(map(str, command[:3])) + "\n" + result.stdout + result.stderr)
    return result.stdout


def main():
    global OUT
    parser = argparse.ArgumentParser()
    parser.add_argument("--toolchain", type=Path)
    parser.add_argument("--compat-package", action="store_true", help="Build the 32 MB SDK application/model layout")
    parser.add_argument("--signer", type=Path, default=Path("F:/STM32CubeProgrammer/bin/STM32_SigningTool_CLI.exe"))
    args = parser.parse_args()
    publish = ROOT / ("Binary/compat" if args.compat_package else "Binary")
    if args.compat_package:
        OUT = ROOT / "Build/ota-compat"
    OUT.mkdir(parents=True, exist_ok=True)
    publish.mkdir(parents=True, exist_ok=True)
    toolchain = args.toolchain
    if toolchain is None:
        matches = list(ROOT.parent.parent.glob("STM32CubeIDE_*/STM32CubeIDE/plugins/*gnu-tools*/tools/bin"))
        if not matches:
            raise RuntimeError("Pass --toolchain with the CubeIDE GNU tools bin directory")
        toolchain = matches[0]
    suffix = ".exe" if os.name == "nt" else ""
    gcc = toolchain / ("arm-none-eabi-gcc" + suffix)
    objcopy = toolchain / ("arm-none-eabi-objcopy" + suffix)
    nm = toolchain / ("arm-none-eabi-nm" + suffix)
    objdump = toolchain / ("arm-none-eabi-objdump" + suffix)
    includes = ["Appli/Core/Inc", "Secure_nsclib", "Drivers/STM32N6xx_HAL_Driver/Inc",
                "Drivers/STM32N6xx_HAL_Driver/Inc/Legacy", "Drivers/CMSIS/Include",
                "Drivers/CMSIS/Device/ST/STM32N6xx/Include", "Middlewares/THREADX/common/inc",
                "Middlewares/THREADX/ports/cortex_m55/gnu/inc", "Middlewares/NETXDUO/common/inc",
                "Middlewares/NETXDUO/addons/dhcp", "Middlewares/NETXDUO/common/drivers/ethernet",
                "Middlewares/NETXDUO/ports/cortex_m55/gnu/inc", "Middlewares/NETXDUO/tsn/inc"]
    includes += [str(p.relative_to(ROOT)) for p in (ROOT / "Appli/Drivers/BSP").iterdir() if p.is_dir()]
    base = ["-mcpu=cortex-m55", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard", "-mcmse",
            "--specs=nano.specs", "-Os", "-ffunction-sections", "-fdata-sections", "-Wall",
            "-DUSE_HAL_DRIVER", "-DSTM32N647xx"]
    if args.compat_package:
        base.append("-DLED2_PACKAGE_COMPAT")
    rtos = ["-DTX_SINGLE_MODE_SECURE", "-DTX_INCLUDE_USER_DEFINE_FILE", "-DNX_INCLUDE_USER_DEFINE_FILE"]
    middleware = []
    for folder in ["NETXDUO/common/src", "NETXDUO/addons/dhcp", "NETXDUO/common/drivers/ethernet",
                   "NETXDUO/tsn/src", "THREADX/common/src", "THREADX/ports/cortex_m55/gnu/src"]:
        middleware += sorted((ROOT / "Middlewares" / folder).glob("*.c"))
        middleware += sorted((ROOT / "Middlewares" / folder).glob("*.S"))
    links = ET.parse(ROOT / "Appli/.project").findall(".//linkedResources/link")
    hal = [ROOT / p.findtext("locationURI").replace("PARENT-1-PROJECT_LOC/", "")
           for p in links if "HAL_Driver" in p.findtext("name", "")]
    core = ROOT / "Appli/Core/Src"
    common_names = ["nx_stm32_phy_driver.c", "secure_nsc.c", "stm32n6xx_hal_msp.c",
                    "stm32n6xx_it.c", "syscalls.c", "sysmem.c", "system_stm32n6xx_s.c",
                    "tx_initialize_low_level.S"]
    common = hal + middleware + [core / n for n in common_names]
    common += [ROOT / "Appli/Core/Startup/startup_stm32n647x0hxq.s"]
    common += [ROOT / f"Appli/Drivers/BSP/{name}/{name.lower()}.c" for name in ("UART", "YT8512C")]

    def compile_many(sources, flags, paths, folder):
        folder.mkdir(parents=True, exist_ok=True)
        inc = ["-I" + str(ROOT / p) for p in paths]

        def compile_one(source):
            tag = hashlib.sha256(str(source).encode()).hexdigest()[:12]
            obj = folder / (source.stem + "_" + tag + ".o")
            language = ["-x", "assembler-with-cpp"] if source.suffix.lower() == ".s" else ["-std=gnu11"]
            command = [gcc] + base + flags + inc + language + ["-c", source, "-o", obj]
            run(command, obj.with_suffix(".log"))
            return obj

        with ThreadPoolExecutor(max_workers=8) as pool:
            return list(pool.map(compile_one, sources))

    def link(name, objects, script):
        directory = OUT / name
        directory.mkdir(parents=True, exist_ok=True)
        response = directory / "objects.list"
        response.write_text("\n".join('"' + p.as_posix() + '"' for p in objects), encoding="utf-8")
        elf = directory / (name + ".elf")
        run([gcc, "-o", elf, "@" + str(response)] + base[:5] +
            ["-T" + str(ROOT / script), "--specs=nosys.specs", "--specs=nano.specs", "-static",
             "-Wl,--gc-sections", "-Wl,-Map=" + str(elf.with_suffix(".map")),
             "-Wl,--cmse-implib", "-Wl,--out-implib=" + str(directory / "secure_nsclib.o"),
             "-Wl,--start-group", "-lc", "-lm", "-Wl,--end-group"], directory / "link.log")
        binary = elf.with_suffix(".bin")
        run([objcopy, "-O", "binary", elf, binary])
        symbols = run([nm, "-S", elf])
        (directory / "symbols.txt").write_text(symbols, encoding="utf-8")
        if name in ("LED_2_Appli", "LED_2_Agent"):
            frame = next(line.split() for line in symbols.splitlines()
                         if line.split()[-1:] == ["ota_agent_main"])
            if int(frame[0], 16) % 32 + int(frame[1], 16) > 16384:
                raise RuntimeError("SRAM executor exceeds its copied frame: " + name)
            disassembly = run([objdump, "-d", "--disassemble=ota_agent_main", elf])
            if re.search(r"\t(?:bl|blx)(?:\.[a-z]+)?\s", disassembly):
                raise RuntimeError("SRAM executor calls code outside its frame: " + name)
            (directory / "sram-executor.asm").write_text(disassembly, encoding="utf-8")
        print(f"{name}: {binary.stat().st_size:,} bytes", flush=True)
        return binary, symbols

    print("Building shared HAL and RTOS sources...", flush=True)
    common_objects = compile_many(common, rtos, includes, OUT / "common")
    app_sources = [p for p in core.glob("*.c") if p.name not in common_names]
    app_sources += [p for p in (ROOT / "Appli/Drivers/BSP").rglob("*.c") if p.parent.name not in ("UART", "YT8512C")]
    app_objects = compile_many(app_sources, rtos, includes, OUT / "app-objects")
    app_bin, _ = link("LED_2_Appli", common_objects + app_objects,
                      "Appli/STM32N647X0HXQ_ROMxspi2_RAMxspi1.ld")
    agent_sources = sorted((ROOT / "Agent/Core/Src").glob("*.c")) + [core / "app_netxduo.c"]
    agent_objects = compile_many(agent_sources, rtos + ["-DLED2_OTA_AGENT"], includes, OUT / "agent-objects")
    agent_script = ROOT / "Agent/STM32N647X0HXQ_ROMxspi2_RAMxspi1_agent.ld"
    agent_base = 0x71F00400 if args.compat_package else 0x71400400
    if args.compat_package:
        script = agent_script.read_text(encoding="utf-8").replace("ORIGIN = 0x71400400", "ORIGIN = 0x71F00400").replace("LENGTH = 4095K", "LENGTH = 1023K")
        agent_script = OUT / "agent-compat.ld"
        agent_script.write_text(script, encoding="utf-8")
    agent_bin, symbols = link("LED_2_Agent", common_objects + agent_objects, agent_script)
    for forbidden in ("ota_flash_write_image", "ota_confirm_boot", "ota_confirm_poll", "ota_request_agent"):
        if any(line.split()[-1:] == [forbidden] for line in symbols.splitlines()):
            raise RuntimeError("Agent contains forbidden function " + forbidden)
    fsbl_includes = ["FSBL/Core/Inc"] + [p for p in includes if p.startswith("Drivers/")]
    extmem = ROOT / "Middlewares/ST/STM32_ExtMem_Manager"
    fsbl_includes += [str(p.relative_to(ROOT)) for p in [extmem] + [p for p in extmem.iterdir() if p.is_dir()]]
    fsbl_includes += ["Drivers/BSP/HyperRAM", "Drivers/BSP/NORFlash"]
    fsbl_sources = sorted((ROOT / "FSBL/Core/Src").glob("*.c"))
    fsbl_sources += [ROOT / "FSBL/Core/Startup/startup_stm32n647x0hxq_fsbl.s", ROOT / "Drivers/BSP/HyperRAM/hyperram.c"]
    fsbl_hal = ["hal", "hal_cortex", "hal_dma", "hal_dma_ex", "hal_exti", "hal_gpio",
                "hal_pwr", "hal_pwr_ex", "hal_rcc", "hal_rcc_ex", "hal_uart", "hal_xspi"]
    fsbl_sources += [ROOT / f"Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_{n}.c" for n in fsbl_hal]
    fsbl_sources += sorted(extmem.rglob("*.c"))
    fsbl_sources += sorted((ROOT / "Drivers/BSP/NORFlash").glob("*.c"))
    fsbl_objects = compile_many(fsbl_sources, [], fsbl_includes, OUT / "fsbl-objects")
    fsbl_bin, fsbl_symbols = link("LED_2_FSBL", fsbl_objects, "FSBL/STM32N647X0HXQ_AXISRAM2_fsbl.ld")
    for binary, base_address in ((app_bin, 0x70100400), (agent_bin, agent_base), (fsbl_bin, 0x34180400)):
        data = binary.read_bytes()
        sp, reset = struct.unpack_from("<II", data)
        if not (0x34000000 <= sp <= 0x34200000 and not sp & 7 and reset & 1 and
                base_address <= (reset & ~1) < base_address + len(data)):
            raise RuntimeError("Invalid vector table: " + str(binary))
    for required in ("EXTMEM_DRIVER_USER_Init", "EXTMEM_DRIVER_USER_Write", "EXTMEM_DRIVER_USER_EraseSector",
                     "EXTMEM_DRIVER_USER_Enable_MemoryMappedMode", "EXTMEM_DRIVER_USER_GetMapAddress"):
        if not any(line.split()[-2:] == ["T", required] for line in fsbl_symbols.splitlines()):
            raise RuntimeError("FSBL is missing the real NOR driver: " + required)
    trusted = fsbl_bin.with_name("LED_2_FSBL-trusted.bin")
    if trusted.exists():
        trusted.chmod(stat.S_IREAD | stat.S_IWRITE)
    trusted.unlink(missing_ok=True)
    run([args.signer, "-s", "-bin", fsbl_bin, "-nk", "-of", "0x80000000", "-t", "fsbl",
         "-o", trusted, "-hv", "2.3", "-align"], OUT / "LED_2_FSBL/sign.log")
    verified_header = verify_fsbl_image(fsbl_bin.read_bytes(), trusted.read_bytes())
    print(f"FSBL ROM header checked: payload +0x400, entry=0x{verified_header['entry']:08X}", flush=True)
    # Publish only after every compile, link, symbol check and signature succeeds.
    manifest = {}
    for binary, name, address in [(app_bin, "appli", "0x70100400"),
                                  (agent_bin, "agent", hex(agent_base)),
                                  (trusted, "fsbl_universal", "0x70000000")]:
        dest = OUT / (name + ".hex")
        run([objcopy, "-I", "binary", binary, "--change-addresses", address, "-O", "ihex", dest])
        shutil.copyfile(dest, publish / dest.name)
        manifest[dest.name] = {"base": address, "bytes": binary.stat().st_size,
                               "sha256": hashlib.sha256(dest.read_bytes()).hexdigest()}
    shutil.copyfile(app_bin, publish / "appli.bin")
    (publish / "ota_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if args.compat_package:
        import zlib
        from ota_package import hex_segments
        from migrate_ota_layout import write_hex
        parts = []
        for name in ("fsbl_universal.hex", "agent.hex", "appli.hex"):
            parts.extend(hex_segments((publish / name).read_text(encoding="ascii")))
        raw = app_bin.read_bytes()
        prefix = bytearray(b"\xff" * 0x400)
        metadata = struct.pack("<IIIIIII", 0x3254524F, 0, len(raw), zlib.crc32(raw), 0, 0, 0)
        prefix[:32] = metadata + struct.pack("<I", zlib.crc32(metadata))
        parts += [(0x70100000, bytes(prefix))]
        parts += [(addr, b"\xff" * 4096) for addr in (0x700FC000, 0x70B00000, 0x71500000)]
        installer = publish / "initial_install.hex"
        write_hex(parts, installer)
        manifest[installer.name] = {"base": "multiple", "bytes": sum(len(data) for _, data in parts),
                                    "sha256": hashlib.sha256(installer.read_bytes()).hexdigest()}
        (publish / "ota_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("Published to " + str(publish), flush=True)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError) as exc:
        print(exc, file=sys.stderr)
        sys.exit(1)
