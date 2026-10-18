"""Create ready-to-send SDK packages without editing the vendor examples."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from ota_package import merge_sdk_images, make_package, unpack_package

DEFAULT_SDK = Path(r"F:\stm32CubeIde\Project\Software_Package\Software_Package")


def package_project(project, output):
    binary = project / "Binary"
    app = binary / "appli.hex"
    ai = any(part.startswith("99_Applications") for part in project.parts)
    network = binary / "network-data.hex"
    if ai and not network.is_file():
        raise ValueError("AI application has no matching network-data.hex")
    image, segments = merge_sdk_images(app.read_text(encoding="ascii"),
                        network.read_text(encoding="ascii") if ai else None)
    profile = 2 if ai else 1
    name = "__".join(project.parts[project.parts.index("Projects") + 1:]) if "Projects" in project.parts else project.name
    package = make_package(image, profile, project.name)
    unpack_package(package)
    dest = output / (name + ".ota")
    dest.write_bytes(package)
    sources = [app] + ([network] if ai else [])
    record = {"project": name, "profile": profile, "clock_mhz": 800 if ai else 600,
              "image_bytes": len(image), "package": str(dest.resolve()), "segments": segments,
              "sha256": hashlib.sha256(package).hexdigest(),
              "sources": [{"path": str(p), "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in sources]}
    dest.with_suffix(".json").write_text(json.dumps(record, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return record


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, default=DEFAULT_SDK)
    parser.add_argument("--project", type=Path, help="One example directory containing Binary/appli.hex")
    parser.add_argument("--out", type=Path, default=ROOT / "Binary/sdk_packages")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    projects = [args.project] if args.project else sorted(p.parent.parent for p in (args.sdk / "Projects").rglob("Binary/appli.hex"))
    if not projects:
        raise SystemExit("No Binary/appli.hex files found")
    prepared, failed = [], []
    for project in projects:
        try:
            prepared.append(package_project(project, args.out))
        except (OSError, ValueError) as exc:
            failed.append({"project": str(project), "error": str(exc)})
    inventory = {"sdk": str(args.sdk), "prepared": prepared, "failed": failed,
                 "validation": "Address, vector, paired resources and CRC checks only; hardware operation is not yet verified."}
    (args.out / "inventory.json").write_text(json.dumps(inventory, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    lines = ["# SDK 兼容升级包", "", "地址、向量、配套模型和 CRC 检查已完成；这些结果不代表所有例程已上板运行。", "",
             "先安装 Binary/compat 的配套 FSBL 与 Agent，再从原网页发送下面的 .ota 文件。", "",
             "| 例程 | 启动时钟 | 应用与模型合并大小 | 升级文件 |", "|---|---:|---:|---|"]
    for r in prepared:
        name = Path(r["package"]).name
        lines.append(f'| {r["project"]} | {r["clock_mhz"]} MHz | {r["image_bytes"]:,} B | [{name}]({name}) |')
    if failed:
        lines += ["", "## 未生成", ""] + [f'- {r["project"]}: {r["error"]}' for r in failed]
    (args.out / "README.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Prepared {len(prepared)} packages; failed {len(failed)}")
    for r in failed:
        print(r["project"], r["error"])
    for r in sorted(prepared, key=lambda r: r["image_bytes"], reverse=True)[:6]:
        print(r["project"], r["image_bytes"], "bytes", r["clock_mhz"], "MHz")
    print("Output:", args.out)
    return bool(failed)


if __name__ == "__main__":
    sys.exit(main())
