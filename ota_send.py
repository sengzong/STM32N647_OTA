#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Start the sender, then reset the board to enter its independent OTA window."""
import argparse
from pathlib import Path
import socket
import sys
from ota_protocol import make_transfer, make_rollback, send_firmware


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--file", default=str(Path(__file__).parent / "Binary/appli.bin"))
    parser.add_argument("--desc", default="Application")
    parser.add_argument("--yes", action="store_true", help="无需再次确认")
    parser.add_argument("--rollback", action="store_true", help="恢复上一版固件，无需提供文件")
    args = parser.parse_args()
    try:
        if args.rollback:
            payload, crc = make_rollback()
            print("请求回滚到上一版固件")
        else:
            payload, crc = make_transfer(args.file, Path(args.file).read_bytes(), args.desc)
            print(f"升级数据 {len(payload):,} 字节，CRC32={crc:08X}")
        if not args.yes and input("确认发送给板子(y/N)? ").strip().lower() not in ("y", "yes"):
            return 0
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.settimeout(180)
            listener.bind((args.bind, args.port))
            listener.listen(1)
            print(f"监听 {args.bind}:{args.port}；现在复位或重新上电板子，等待连接…")
            conn, addr = listener.accept()
            with conn:
                print(f"板子已连接 {addr[0]}，正在发送并等待写入确认…")
                slot = send_firmware(conn, payload, crc)
        if args.rollback:
            print("板子已校验回滚备份并接受请求，即将恢复上一版固件。")
        else:
            print("板子已校验并保存最新固件。Agent 模式会自动启动；应用模式需在屏幕确认切换。")
        return 0
    except (OSError, ValueError) as exc:
        print(f"升级失败：{exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
