"""Rebuild SDK 991 with the local three-buffer source overlay."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import difflib
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from ota_package import hex_segments, merge_sdk_images, make_package, unpack_package

SDK = Path(r"F:/stm32CubeIde/Project/Software_Package/Software_Package")
OVERLAY = ROOT / "SDK_Adapted/991_AI_People_Detection"
OUT = ROOT / "Build/991-triple-buffer"
PUBLISH = ROOT / "Binary/991_triple_buffer"
NAME = "991_AI_People_Detection_Appli"
CAMERA_MODE_OVERRIDES = {
    "Appli/Core/Src/app_camera.c",
    "Middlewares/STM32_MW_CAMERA/sensors/cmw_imx335.c",
    "Middlewares/STM32_MW_CAMERA/sensors/imx335/imx335.c",
}


def make_values(text, key):
    match = re.search(r"^" + key + r"\s*\+=\s*(.*?)(?=\n\s*\n)", text, re.M | re.S)
    return shlex.split(match[1].replace("\\\n", " ")) if match else []


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, cwd, log):
    result = subprocess.run([str(x) for x in command], cwd=cwd, input="",
                            capture_output=True, encoding="utf-8", errors="replace", timeout=120)
    log.write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"Command failed; see {log}\n{result.stdout}{result.stderr}")
    return result.stdout


def main():
    global OVERLAY, OUT, PUBLISH
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, default=SDK)
    parser.add_argument("--toolchain", type=Path)
    variants = parser.add_mutually_exclusive_group()
    variants.add_argument("--performance", action="store_true", help="Internal SRAM inputs and optimized CPU stages")
    variants.add_argument("--realtime", action="store_true", help="Latest-frame capture and persistent NPU")
    parser.add_argument("--capture60", action="store_true", help="Experimental BIN60 sensor mode; requires --realtime")
    args = parser.parse_args()
    if args.capture60 and not args.realtime:
        parser.error("--capture60 requires --realtime")
    speed = args.performance or args.realtime
    if args.capture60:
        print("WARNING: RT60 failed board image validation (noise); retained only for investigation.", flush=True)
    label = "991 speed SRAM" if args.performance else "991 triple buffer"
    package_name = "991_performance.ota" if args.performance else "991_triple_buffer.ota"
    if args.performance:
        OVERLAY = ROOT / "SDK_Adapted/991_Performance"
        OUT = ROOT / "Build/991-performance"
        PUBLISH = ROOT / "Binary/991_performance"
    if args.realtime:
        variant = "60_trial" if args.capture60 else "30"
        label = "991 BIN60 trial" if args.capture60 else "991 RT30 R2"
        package_name = "991_realtime_" + variant + ".ota"
        OVERLAY = ROOT / "SDK_Adapted/991_Realtime"
        OUT = ROOT / ("Build/991-realtime-" + variant)
        PUBLISH = ROOT / ("Binary/991_realtime_" + variant)
    project = args.sdk / "Projects/99_Applications/991_AI_People_Detection"
    release = project / "STM32CubeIDE/Appli/Release"
    toolchain = args.toolchain
    if toolchain is None:
        matches = sorted(ROOT.parent.parent.glob("STM32CubeIDE_*/STM32CubeIDE/plugins/*gnu-tools*/tools/bin"))
        if not matches:
            raise RuntimeError("Pass --toolchain with the CubeIDE GNU tools bin directory")
        toolchain = matches[0]
    gcc = toolchain / "arm-none-eabi-gcc.exe"
    OUT.mkdir(parents=True, exist_ok=True)
    PUBLISH.mkdir(parents=True, exist_ok=True)

    def use_override(relative):
        # RT30 must use the known-working SDK camera pipeline without mode changes.
        return not (args.realtime and not args.capture60 and relative in CAMERA_MODE_OVERRIDES)

    # Quote-includes within vendor headers must also see the new queue capacity.
    headers = OUT / "include"
    shutil.copytree(project / "Appli/Core/Inc", headers, dirs_exist_ok=True)
    shutil.copytree(OVERLAY / "Appli/Core/Inc", headers, dirs_exist_ok=True)
    patches = []
    for local_source in sorted(OVERLAY.rglob("*")):
        if local_source.suffix not in (".c", ".h"):
            continue
        relative = local_source.relative_to(OVERLAY).as_posix()
        if not use_override(relative):
            continue
        original_source = project / relative
        if not original_source.exists():
            original_source = args.sdk / relative
        patches.extend(difflib.unified_diff(
            original_source.read_text(encoding="utf-8").splitlines(True) if original_source.exists() else [],
            (OVERLAY / relative).read_text(encoding="utf-8").splitlines(True),
            fromfile="original/" + relative, tofile="modified/" + relative))
    (OUT / "source.patch").write_text("".join(patches), encoding="utf-8")

    units = {}
    for mk in sorted(release.rglob("subdir.mk")):
        text = mk.read_text(encoding="utf-8")
        sources = sum((make_values(text, key) for key in ("C_SRCS", "S_SRCS", "S_UPPER_SRCS")), [])
        objects = make_values(text, "OBJS")
        if len(sources) != len(objects):
            raise ValueError(f"Source/object mismatch in {mk}")
        recipes = list(dict.fromkeys(line.strip() for line in text.splitlines()
                                    if line.startswith("\tarm-none-eabi-gcc ")))
        objects_by_stem = {Path(name).stem: name for name in objects}
        if len(objects_by_stem) != len(objects):
            raise ValueError(f"Ambiguous object basenames in {mk}")
        for source_name in sources:
            object_name = objects_by_stem.pop(Path(source_name).stem)
            source = (release / source_name).resolve()
            original = source
            for base in (project, args.sdk):
                if source.is_relative_to(base.resolve()):
                    candidate = OVERLAY / source.relative_to(base.resolve())
                    if candidate.is_file() and use_override(candidate.relative_to(OVERLAY).as_posix()):
                        source = candidate
                        break
            assembly = source.suffix.lower() == ".s"
            options = [shlex.split(recipe)[1:] for recipe in recipes
                       if ("-x assembler-with-cpp" in recipe) == assembly]
            if len(options) != 1:
                raise ValueError(f"Ambiguous compiler recipe for {source}")
            obj = OUT / "objects" / object_name.removeprefix("./")
            obj.parent.mkdir(parents=True, exist_ok=True)
            flags = []
            for option in options[0]:
                if option in ("-MMD", "-MP", "-fstack-usage", "-fcyclomatic-complexity"):
                    continue
                if option.startswith(("-MF", "-MT")):
                    continue
                compiler_source = source.as_posix() if source != original else source_name
                flags.append(option.replace("$<", compiler_source).replace("$@", str(obj)))
            key = object_name.removeprefix("./")
            if args.capture60:
                flags.append("-DAPP_CAPTURE_BIN60=1")
            if speed and (key in ("Application/User/Core/app.o", "Application/User/Core/app_postprocess.o", "Model/network.o")
                                     or key.startswith("Middlewares/AI/Npu/ll_aton/ll_aton_lib")):
                flags = ["-O3" if option == "-Os" else option for option in flags]
            if key in units or not source.is_file():
                raise ValueError(f"Duplicate object or missing source: {source}")
            units[key] = {"source": str(source), "original": str(original), "object": str(obj),
                          "sha256": sha256(source), "recipe": str(mk),
                          "command": [str(gcc), "-I" + str(headers)] + flags}
        if objects_by_stem:
            raise ValueError(f"Objects without matching sources in {mk}")

    ordered_names = [name.removeprefix("./") for name in shlex.split((release / "objects.list").read_text())]
    if len(set(ordered_names)) != len(ordered_names) or set(ordered_names) != set(units):
        raise ValueError("Build recipes do not cover the complete Release object list")
    ordered = [units[key] for key in ordered_names]
    (OUT / "commands.json").write_text(json.dumps(ordered, indent=2) + "\n", encoding="utf-8")
    print(f"Rebuilding {len(ordered)} source files; SDK sources are read-only", flush=True)

    def compile_one(unit):
        obj = Path(unit["object"])
        run(unit["command"], release, obj.with_suffix(".log"))

    with ThreadPoolExecutor(max_workers=8) as pool:
        list(pool.map(compile_one, ordered))

    response = OUT / "objects.list"
    response.write_text("\n".join('"' + Path(unit["object"]).as_posix() + '"' for unit in ordered), encoding="utf-8")
    makefile = (release / "makefile").read_text()
    recipe = next(line.strip() for line in makefile.splitlines() if line.startswith("\tarm-none-eabi-gcc -o "))
    libraries = (release / "objects.mk").read_text().split("LIBS :=", 1)[1].strip()
    recipe = recipe.replace("$(USER_OBJS)", "").replace("$(LIBS)", libraries)
    link = shlex.split(recipe)
    link[0] = str(gcc)
    link = ["@" + str(response) if token == "@objects.list" else token for token in link]
    elf = OUT / (NAME + ".elf")
    run(link, OUT, OUT / "link.log")
    for fmt, extension in (("binary", "bin"), ("ihex", "hex")):
        run([toolchain / "arm-none-eabi-objcopy.exe", "-O", fmt, elf, OUT / ("appli." + extension)],
            OUT, OUT / ("objcopy-" + extension + ".log"))
    symbols = run([toolchain / "arm-none-eabi-nm.exe", "-S", "--defined-only", elf], OUT, OUT / "symbols.log")
    match = re.search(r"^(\w+)\s+(\w+)\s+\w\s+nn_input_buffers$", symbols, re.M)
    expected_size = 3 * 320 * 320 * 3
    address = int(match[1], 16) if match else 0
    expected_region = (0x34000000 <= address and address + expected_size <= 0x34200000) if speed else address == 0x91000000
    if not match or not expected_region or int(match[2], 16) != expected_size:
        raise ValueError("Linked input buffer placement/size is not three 320x320 RGB buffers")
    sections = run([toolchain / "arm-none-eabi-objdump.exe", "-h", elf], OUT, OUT / "sections.log")
    section = re.search(r"\s\.EXTRAM\s+(\w+)\s+(\w+)", sections)
    if not section or int(section[2], 16) != 0x91000000 or int(section[1], 16) > 0x1000000:
        raise ValueError("External RAM section exceeds the original SDK memory region")
    bss_end = re.search(r"^(\w+)\s+\w\s+_ebss$", symbols, re.M)
    if not bss_end or int(bss_end[1], 16) > 0x341D0000:
        raise ValueError("Application leaves insufficient internal RAM for heap/system stack")
    disassemble_symbols = ["app_run", "app_camera_nn_pipe_frame_cb"]
    if speed:
        converter = re.search(r"^\w+\s+\w+\s+\w\s+(app_prepare_input\S*)$", symbols, re.M)
        if not converter:
            raise ValueError("Input preparation symbol is missing")
        disassemble_symbols.append(converter[1])
    for symbol in disassemble_symbols:
        run([toolchain / "arm-none-eabi-objdump.exe", "-d", "-marmv8.1-m.main", "--disassemble=" + symbol, elf],
            OUT, OUT / (symbol + ".asm"))
    if speed:
        conversion_asm = (OUT / (converter[1] + ".asm")).read_text()
        if "veor" not in conversion_asm or "vldrb" not in conversion_asm or "vstrb" not in conversion_asm:
            raise ValueError("Input conversion was not compiled to MVE instructions")

    app = (OUT / "appli.hex").read_text(encoding="ascii")
    network = project / "Binary/network-data.hex"
    model = network.read_text(encoding="ascii")
    image, segments = merge_sdk_images(app, model)
    package = make_package(image, 2, label)
    if unpack_package(package) != (image, 2, label):
        raise ValueError("Package verification failed")
    # A single addressed HEX is also useful when updating through Programmer.
    app_lines = [line for line in app.splitlines() if int(line[7:9], 16) != 1]
    combined = "\n".join(app_lines + model.splitlines()) + "\n"
    if hex_segments(combined) != hex_segments(app) + hex_segments(model):
        raise ValueError("Combined HEX does not preserve application/model addresses")
    for extension in ("bin", "hex"):
        shutil.copy2(OUT / ("appli." + extension), PUBLISH / ("appli." + extension))
    if args.performance:
        shutil.copy2(OUT / "appli.hex", PUBLISH / "991_performance.hex")
    if args.realtime:
        shutil.copy2(OUT / "appli.hex", PUBLISH / ("991_realtime_" + variant + ".hex"))
    shutil.copy2(network, PUBLISH / "network-data.hex")
    (PUBLISH / package_name).write_bytes(package)
    (PUBLISH / "application_and_network.hex").write_text(combined, encoding="ascii")
    manifest = {"name": label, "profile": 2, "clock_mhz": 800,
                "input_buffers": 3, "extra_psram_bytes": 0 if speed else 307200,
                "input_address": hex(address), "input_bytes": expected_size,
                "internal_bss_end": hex(int(bss_end[1], 16)),
                "speed_optimized": speed,
                "capture_target_fps": 60 if args.capture60 else 30,
                "capture_size": [1296, 972] if args.capture60 else [2592, 1944],
                "latest_frame": args.realtime, "persistent_runtime": args.realtime,
                "camera_pipeline": "experimental_binning" if args.capture60 else "original_sdk",
                "camera_board_status": "failed_noise_reported_2026-10-18" if args.capture60 else "new_application_needs_board_check",
                "source_overrides": [{"path": str(Path(unit["source"]).relative_to(ROOT)),
                                      "sha256": unit["sha256"]}
                                     for unit in ordered if unit["source"] != unit["original"]],
                "o3_objects": [key for key in ordered_names if "-O3" in units[key]["command"]],
                "extram_bytes": int(section[1], 16), "segments": segments,
                "rebuilt_source_files": len(ordered), "model_sha256": sha256(network),
                "overlay": [{"path": str(path.relative_to(ROOT)), "sha256": sha256(path)}
                            for path in sorted(OVERLAY.rglob("*")) if path.suffix in (".c", ".h")],
                "files": {path.name: sha256(path) for path in sorted(PUBLISH.iterdir())
                          if path.suffix in (".bin", ".hex", ".ota")},
                "validation": "Target build, buffer placement, model pairing and CRC verified; FPS improvement requires board testing."}
    (PUBLISH / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    shutil.copy2(OVERLAY / "README.md", PUBLISH / "README.md")
    print(f"Build OK. Input: {hex(address)}, {expected_size} B; .EXTRAM: {int(section[1], 16)} B", flush=True)
    print(f"Package: {PUBLISH / package_name}", flush=True)


if __name__ == "__main__":
    main()
