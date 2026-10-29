#!/usr/bin/env python3
"""SMIB host simulator: mux splitter + fake EGM (SAS) on the serial link. This
is the SMIB's SAS test bench AND the reference for the production splitter app.

The CMS is NOT here any more. The terminal reaches it over Wi-Fi, so run the
fake CMS alongside this and point the board at it (admin panel, or .env):

    python cms_tcp_server.py --port 9020      # the CMS, and the bar console

This process now carries only what is physically on the wire to the machine:
SAS frames (channel 0x01) and the firmware's logs (channel 0x7F).

Usage:
    python host_sim.py --port /dev/cu.usbserial-XXXX [--baud 115200]
        [--aft-status 0x80]        EGM fails AFT transfers -> rollback path
        [--aft-pending N]          EGM stays 'pending' for N interrogates
        [--sas-verbose]            print every SAS exchange

The CMS-side fault flags (--fail-auth, --fail-debit, --drop-commit,
--drop-credit) moved to cms_tcp_server.py, along with the bar console that
drives the order ring (type `orders` there).
"""
import argparse
import sys

import serial

from mux import CH_CMS, CH_LOG, CH_SAS, MuxParser, frame
from sas_egm import FakeEgm


def parse_args():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", required=True)
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--aft-status", type=lambda s: int(s, 0), default=0x00)
    p.add_argument("--aft-pending", type=int, default=0)
    p.add_argument("--sas-verbose", action="store_true")
    return p.parse_args()


def main():
    args = parse_args()

    egm = FakeEgm(aft_status=args.aft_status, aft_pending_polls=args.aft_pending,
                  verbose=args.sas_verbose)

    # Opening the port toggles DTR/RTS, which resets the ESP32 on most CYD
    # boards; expect boot-ROM garbage, the parser resyncs past it.
    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = args.baud
    ser.timeout = 0.05
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.reset_input_buffer()

    parser = MuxParser()
    print(f"[SIM] listening on {args.port} @ {args.baud} "
          f"(SAS + logs; AFT status {args.aft_status:#04x})")
    print("[SIM] the CMS is over Wi-Fi: run cms_tcp_server.py separately")

    stats = {"sas": 0, "log": 0, "cms": 0}
    warned_cms = False

    try:
        while True:
            data = ser.read(4096)
            if not data:
                continue
            for chan, payload in parser.feed(data):
                if chan == CH_LOG:
                    stats["log"] += 1
                    print(f"\x1b[90m[SMIB]\x1b[0m {payload.decode(errors='replace')}")
                elif chan == CH_SAS:
                    stats["sas"] += 1
                    if args.sas_verbose:
                        print(f"[SAS<] {payload.hex(' ')}")
                    resp = egm.handle(payload)
                    if resp is not None:
                        if args.sas_verbose:
                            print(f"[SAS>] {resp.hex(' ')}")
                        ser.write(frame(CH_SAS, resp))
                elif chan == CH_CMS:
                    # Nothing should send CMS over the wire now. Say so once
                    # rather than silently dropping it: a CMS frame here means
                    # firmware older than the Wi-Fi port, and it will sit
                    # waiting for a reply that is never coming.
                    stats["cms"] += 1
                    if not warned_cms:
                        warned_cms = True
                        print("[SIM] WARNING: a CMS frame arrived on the serial link. "
                              "This firmware predates the Wi-Fi CMS port, or the mux "
                              "channels are crossed. Nothing here will answer it.")
    except KeyboardInterrupt:
        print(f"\n[SIM] frames: {stats}  |  EGM credits {egm.credits / 100:.2f}")
        return 0


if __name__ == "__main__":
    sys.exit(main())
