#!/usr/bin/env python3
"""FakeCms behind a TCP socket, speaking mux-framed CMS JSON.

Counterpart of the slot game's CMS bridge: when the game owns the serial
port to the SMIB, it forwards channel-2 (CMS) frames to this server over TCP
and writes the replies back onto the wire. Framing on the TCP leg is the same
mux format as the serial leg (frame(CH_CMS, json-bytes)).

Usage:
    python cms_tcp_server.py [--host 0.0.0.0] [--port 9020]
        [--player NAME] [--balance CENTS] [--points N] [--uid UID]
        [--fail-auth] [--fail-debit] [--drop-commit] [--drop-credit]
"""
import argparse
import json
import socket
import threading

from mux import CH_CMS, MuxParser, frame
from cms_server import FakeCms


def parse_args():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default="0.0.0.0")
    p.add_argument("--port", type=int, default=9020)
    p.add_argument("--player", default="Иван Илијевски")
    p.add_argument("--balance", type=int, default=1250000)
    p.add_argument("--points", type=int, default=340)
    p.add_argument("--uid", default=None)
    p.add_argument("--fail-auth", action="store_true")
    p.add_argument("--fail-debit", action="store_true")
    p.add_argument("--drop-commit", action="store_true")
    p.add_argument("--drop-credit", action="store_true")
    return p.parse_args()


def serve_client(conn: socket.socket, addr, cms: FakeCms):
    print(f"[TCP] bridge connected from {addr[0]}:{addr[1]}")
    parser = MuxParser()
    try:
        while True:
            data = conn.recv(4096)
            if not data:
                break
            for chan, payload in parser.feed(data):
                if chan != CH_CMS:
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
                    conn.sendall(frame(CH_CMS, json.dumps(reply, ensure_ascii=False).encode()))
    except ConnectionError:
        pass
    finally:
        conn.close()
        print(f"[TCP] bridge {addr[0]}:{addr[1]} disconnected")


def main():
    args = parse_args()
    cms = FakeCms(player_name=args.player, balance=args.balance, points=args.points,
                  uid=args.uid, fail_auth=args.fail_auth, fail_debit=args.fail_debit,
                  drop_commit=args.drop_commit, drop_credit=args.drop_credit)

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((args.host, args.port))
    server.listen(2)
    print(f"[TCP] FakeCms listening on {args.host}:{args.port}")

    try:
        while True:
            conn, addr = server.accept()
            # One bridge at a time is the normal case; threads keep a stray
            # second connection from wedging the listener.
            threading.Thread(target=serve_client, args=(conn, addr, cms), daemon=True).start()
    except KeyboardInterrupt:
        print(f"\n[TCP] card balance {cms.balance / 100:.2f}, points {cms.points}")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
