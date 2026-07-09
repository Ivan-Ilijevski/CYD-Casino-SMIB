#!/usr/bin/env python3
"""Offline self-test for the host sim: framing resync, EGM SAS handling
(with the exact byte layouts the firmware sends), and CMS flows.
Run: python selftest.py  -> exits 0 and prints OK on success."""
import json

from mux import CH_CMS, CH_SAS, MuxParser, crc16_kermit, frame
from sas_egm import ADDR, AFT_OK, FakeEgm, bcd_to_u64, u64_to_bcd, with_crc
from cms_server import FakeCms


def test_mux():
    p = MuxParser()
    f1 = frame(CH_SAS, bytes([0x81]))
    f2 = frame(CH_CMS, b'{"t":"ping","id":1,"d":{}}')
    # Boot garbage (incl. a lone 0x7E) + corrupted copy + good frames, split
    # across arbitrary feed boundaries.
    corrupted = bytearray(f1)
    corrupted[-1] ^= 0xFF
    stream = b"ets Jun  8 2016 00:22:57\x7e\x01\xff" + bytes(corrupted) + f1 + f2
    out = []
    for i in range(0, len(stream), 3):
        out += p.feed(stream[i:i + 3])
    assert out == [(CH_SAS, bytes([0x81])), (CH_CMS, b'{"t":"ping","id":1,"d":{}}')], out


def long_poll(cmd, body=b""):
    return with_crc(bytes([ADDR, cmd]) + body)


def build_fw_aft_request(amount, txn):
    """Byte-for-byte replica of the firmware's 0x72 full-transfer body."""
    body = bytearray()
    body.append(0)                      # length placeholder
    body += bytes([0x00, 0x00, 0x00])   # code, index, type
    body += u64_to_bcd(amount, 5)
    body += bytes(5) + bytes(5)         # restricted, nonrestricted
    body.append(0x00)                   # flags
    body += (0x00001234).to_bytes(4, "little")
    body += bytes(20)                   # registration key
    body.append(len(txn))
    body += txn.encode()
    body += bytes(4) + bytes([0, 0]) + bytes([0]) + bytes([0, 0])
    body[0] = len(body) - 1
    return long_poll(0x72, bytes(body))


def test_egm():
    egm = FakeEgm()
    # General poll: no activity
    assert egm.handle(bytes([0x81])) == b"\x00"
    # Current credits round-trip
    resp = egm.handle(long_poll(0x1A))
    assert resp[0] == ADDR and resp[1] == 0x1A
    assert crc16_kermit(resp[:-2]) == resp[-2] | (resp[-1] << 8)
    assert bcd_to_u64(resp[2:6]) == 0
    # Version/serial
    resp = egm.handle(long_poll(0x54))
    assert resp[3:6] == b"603"
    # AFT registration interrogate (firmware body: len=1, code 0xFF)
    resp = egm.handle(long_poll(0x73, bytes([0x01, 0xFF])))
    assert resp[3] == 0x01  # ready
    assert int.from_bytes(resp[4:8], "little") == 0x00001234
    # AFT transfer, firmware layout; status must sit at resp[4]
    resp = egm.handle(build_fw_aft_request(50000, "SMIB-000001"))
    assert resp[4] == AFT_OK, resp.hex()
    assert bcd_to_u64(resp[7:12]) == 50000
    assert egm.credits == 50000
    # Completion exception queued for the next general poll
    assert egm.handle(bytes([0x80])) == b"\x69"
    # Failure mode
    egm_fail = FakeEgm(aft_status=0x80)
    resp = egm_fail.handle(build_fw_aft_request(1000, "SMIB-000002"))
    assert resp[4] == 0x80 and egm_fail.credits == 0
    # Pending -> interrogate -> complete
    egm_p = FakeEgm(aft_pending_polls=2)
    resp = egm_p.handle(build_fw_aft_request(2000, "SMIB-000003"))
    assert resp[4] == 0x40
    interrogate = long_poll(0x72, bytes([0x02, 0xFF, 0x00]))
    assert egm_p.handle(interrogate)[4] == 0x40
    assert egm_p.handle(interrogate)[4] == AFT_OK
    assert egm_p.credits == 2000


def rpc(cms, t, mid, **d):
    replies = cms.handle({"t": t, "id": mid, "d": d})
    return replies[0]["d"] if replies else None


def test_cms():
    cms = FakeCms(balance=100000, points=100)
    auth = rpc(cms, "auth_req", 1, uid="AA BB")
    assert auth["ok"] and auth["player"]["balance"] == 100000
    sid = auth["sid"]
    # Debit hold + commit (idempotent)
    deb = rpc(cms, "debit_req", 2, sid=sid, txn="T1", amount=40000)
    assert deb["ok"] and deb["balance"] == 60000
    assert rpc(cms, "debit_commit", 3, txn="T1")["ok"]
    assert rpc(cms, "debit_commit", 4, txn="T1")["ok"]      # repeat commit ok
    # Rollback restores exactly once
    deb = rpc(cms, "debit_req", 5, sid=sid, txn="T2", amount=10000)
    assert deb["balance"] == 50000
    assert rpc(cms, "debit_rollback", 6, txn="T2")["ok"]
    assert cms.balance == 60000
    assert rpc(cms, "debit_rollback", 7, txn="T2")["ok"]    # idempotent
    assert cms.balance == 60000
    # Overdraft refused
    assert not rpc(cms, "debit_req", 8, sid=sid, txn="T3", amount=999999)["ok"]
    # Order with cash, then with points
    order = rpc(cms, "order_req", 9, sid=sid, pay="cash",
                items=[{"drink": 1, "qty": 2}])
    assert order["ok"] and order["balance"] == 60000 - 16000
    order = rpc(cms, "order_req", 10, sid=sid, pay="points",
                items=[{"drink": 2, "qty": 1}])
    assert order["ok"] and order["points"] == 20
    # Unavailable drink -> empty order
    assert not rpc(cms, "order_req", 11, sid=sid, pay="cash",
                   items=[{"drink": 5, "qty": 1}])["ok"]
    # JSON envelope survives UTF-8
    txt = json.dumps({"t": "menu_res", "d": {"items": cms.menu}}, ensure_ascii=False)
    assert "Кафе" in txt


if __name__ == "__main__":
    test_mux()
    test_egm()
    test_cms()
    print("OK: mux framing, EGM SAS subset, CMS flows all pass")
