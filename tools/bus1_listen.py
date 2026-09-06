#!/usr/bin/env python3
"""BUS1 UDP 上位机：配对 + 打印遥测/帧"""

import argparse
import socket
import struct
import time

MAGIC = 0x31535542  # 'BUS1' LE
PORT = 9527
TOKEN = b"bus1-pair-2026"

TYPE_TELEM = 1
TYPE_FRAME = 2
TYPE_ACK = 4
TYPE_HELLO = 5

SRC = {0: "CAN", 1: "485", 2: "UART", 3: "I2C", 4: "SPI"}


def make_pkt(typ, payload: bytes, seq=0):
    return struct.pack(
        "<IBBHIH",
        MAGIC,
        typ,
        0,
        seq,
        int(time.time() * 1000) & 0xFFFFFFFF,
        len(payload),
    ) + payload


def main():
    ap = argparse.ArgumentParser(description="ESP32-P4 BUS1 listener")
    ap.add_argument("ip", help="板端 IP，如 192.168.4.1")
    ap.add_argument("--port", type=int, default=PORT)
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(1.0)
    sock.bind(("", 0))
    peer = (args.ip, args.port)

    sock.sendto(make_pkt(TYPE_HELLO, TOKEN), peer)
    print(f"HELLO -> {peer}")

    while True:
        try:
            data, _addr = sock.recvfrom(2048)
        except socket.timeout:
            sock.sendto(make_pkt(TYPE_HELLO, TOKEN), peer)
            continue
        if len(data) < 14:
            continue
        magic, typ, _flags, _seq, _tick, plen = struct.unpack_from("<IBBHIH", data, 0)
        if magic != MAGIC:
            continue
        payload = data[14 : 14 + plen]
        if typ == TYPE_ACK:
            print("ACK", payload.hex() if payload else "ok")
        elif typ == TYPE_TELEM and len(payload) >= 20:
            rx, tx, err, drop = struct.unpack_from("<IIII", payload, 0)
            rate = struct.unpack_from("<f", payload, 16)[0]
            print(f"TELEM rx={rx} tx={tx} err={err} drop={drop} rate={rate:.1f}/s")
        elif typ == TYPE_FRAME and len(payload) >= 12:
            t_ms, src, direc, flags, ln, fid = struct.unpack_from("<IBBBBI", payload, 0)
            raw = payload[12 : 12 + ln]
            hx = " ".join(f"{b:02X}" for b in raw)
            print(
                f"[{t_ms}] {SRC.get(src, src)} "
                f"{'TX' if direc else 'RX'} id=0x{fid:X} flags={flags:02X} {hx}"
            )


if __name__ == "__main__":
    main()
