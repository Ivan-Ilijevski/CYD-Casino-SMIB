#!/usr/bin/env python3
"""Passive SMIB port listener (no EGM/CMS responses) — the debugging tool
for a console-less board. Prints decoded LOG/SAS/CMS frames AND raw boot
banners, so a crash loop shows up as repeating `rst:0x..` lines.

Usage: python listen.py --port /dev/cu.wchusbserial210 [--seconds 15] [--reset]
"""
import argparse
import re
import time

import serial

from mux import CH_CMS, CH_LOG, CH_SAS, MuxParser


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=15)
    ap.add_argument("--reset", action="store_true",
                    help="pulse RTS to reset the board and observe boot from t=0")
    args = ap.parse_args()

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = args.baud
    ser.timeout = 0.05
    ser.dtr = False
    ser.rts = False
    ser.open()
    if args.reset:
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
    ser.reset_input_buffer()

    parser = MuxParser()
    raw = bytearray()
    t0 = time.time()
    sas = 0
    try:
        while time.time() - t0 < args.seconds:
            data = ser.read(4096)
            if not data:
                continue
            raw.extend(data)
            for chan, payload in parser.feed(data):
                ts = f"{time.time() - t0:6.2f}s"
                if chan == CH_LOG:
                    print(ts, "[LOG]", payload.decode(errors="replace"))
                elif chan == CH_SAS:
                    sas += 1
                    if sas <= 5:
                        print(ts, "[SAS]", payload.hex(" "))
                elif chan == CH_CMS:
                    print(ts, "[CMS]", payload[:120].decode(errors="replace"))
    except KeyboardInterrupt:
        pass

    print(f"--- {sas} SAS frames total ---")
    banners = [s for s in re.findall(r"[\x20-\x7e]{6,}", raw.decode("latin1"))
               if re.search(r"rst:|Guru|overflow|abort|assert|Brownout", s)]
    if banners:
        print("--- reset/panic banners captured: ---")
        for s in banners[:20]:
            print("[RAW]", s)
    else:
        print("--- no reset banners (no crashes in this window) ---")


if __name__ == "__main__":
    main()