#!/usr/bin/env python3
"""SMIB host simulator: mux splitter + fake EGM (SAS) + fake CMS in one
process. This is the SMIB's test bench AND the reference for the production
splitter app (which routes SAS to the real slot game and CMS to the network).

Usage:
    python host_sim.py --port /dev/cu.usbserial-XXXX [--baud 115200]
        [--player "Иван Илиевски"] [--balance 1250000] [--points 340]
        [--uid "9C 76 5A F4"]      only accept this card (default: any)
        [--fail-auth]              reject every auth
        [--fail-debit]             refuse funds holds
        [--aft-status 0x80]        EGM fails AFT transfers -> rollback path
        [--aft-pending N]          EGM stays 'pending' for N interrogates
        [--drop-commit]            swallow first commit -> retry path
        [--drop-credit]            swallow first cashout credit -> retry path
        [--no-cms]                 CMS silent (offline badge test)
        [--sas-verbose]            print every SAS exchange
"""
import argparse
import json
import sys
import time

import serial

from mux import CH_CMS, CH_LOG, CH_SAS, MuxParser, frame
from sas_egm import FakeEgm
from cms_server import FakeCms


def parse_args():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", required=True)
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--player", default="Иван Илијевски")
    p.add_argument("--balance", type=int, default=1250000)
    p.add_argument("--points", type=int, default=340)
    p.add_argument("--uid", default=None)
    p.add_argument("--fail-auth", action="store_true")
    p.add_argument("--fail-debit", action="store_true")
    p.add_argument("--aft-status", type=lambda s: int(s, 0), default=0x00)
    p.add_argument("--aft-pending", type=int, default=0)
    p.add_argument("--drop-commit", action="store_true")
    p.add_argument("--drop-credit", action="store_true")
    p.add_argument("--no-cms", action="store_true")
    p.add_argument("--sas-verbose", action="store_true")
    return p.parse_args()


def main():
    args = parse_args()

    egm = FakeEgm(aft_status=args.aft_status, aft_pending_polls=args.aft_pending,
                  verbose=args.sas_verbose)
    cms = FakeCms(player_name=args.player, balance=args.balance, points=args.points,
                  uid=args.uid, fail_auth=args.fail_auth, fail_debit=args.fail_debit,
                  drop_commit=args.drop_commit, drop_credit=args.drop_credit)

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
          f"(CMS {'OFF' if args.no_cms else 'on'}, AFT status {args.aft_status:#04x})")

    stats = {"sas": 0, "cms": 0, "log": 0}
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
                    stats["cms"] += 1
                    if args.no_cms:
                        continue
                    try:
                        msg = json.loads(payload.decode())
                    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                        print(f"[CMS] bad JSON: {exc}: {payload[:80]!r}")
                        continue
                    if msg.get("t") not in ("ping", "hello"):
                        print(f"[CMS<] {json.dumps(msg, ensure_ascii=False)}")
                    for reply in cms.handle(msg):
                        if reply.get("t") != "pong":
                            print(f"[CMS>] {json.dumps(reply, ensure_ascii=False)}")
                        ser.write(frame(CH_CMS, json.dumps(reply, ensure_ascii=False)
                                        .encode()))
    except KeyboardInterrupt:
        print(f"\n[SIM] frames: {stats}  |  EGM credits {egm.credits / 100:.2f}  |  "
              f"card balance {cms.balance / 100:.2f}, points {cms.points}")
        return 0


if __name__ == "__main__":
    sys.exit(main())
