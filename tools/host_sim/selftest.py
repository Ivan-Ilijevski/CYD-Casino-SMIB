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


def build_fw_aft_request(amount, txn, ttype=0x00):
    """Byte-for-byte replica of the firmware's 0x72 full-transfer body."""
    body = bytearray()
    body.append(0)                      # length placeholder
    body += bytes([0x00, 0x00, ttype])  # code, index, type (0x80 = from EGM)
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
    # Completion exception queued for the next general poll (addressed 0x81)
    assert egm.handle(bytes([0x81])) == b"\x69"
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
    # From-machine transfer (cashout): type 0x80 moves credits off the EGM
    egm_c = FakeEgm()
    egm_c.credits = 30000
    resp = egm_c.handle(build_fw_aft_request(30000, "SMIB-000004", ttype=0x80))
    assert resp[4] == AFT_OK, resp.hex()
    assert resp[6] == 0x80              # transfer type echoed in the response
    assert bcd_to_u64(resp[7:12]) == 30000
    assert egm_c.credits == 0
    assert egm_c.handle(bytes([0x81])) == b"\x69"  # completion exception
    # Cashout larger than the machine's credits -> refused, nothing moved
    egm_c.credits = 1000
    resp = egm_c.handle(build_fw_aft_request(2000, "SMIB-000005", ttype=0x80))
    assert resp[4] == 0x80 and egm_c.credits == 1000
    # G3: the accounting denomination is reported in 0x1F at offset 7
    egm_d = FakeEgm(denom_code=0x03)
    resp = egm_d.handle(long_poll(0x1F))
    assert resp[7] == 0x03, resp.hex()


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
    # Cashout credit: applied once, idempotent by txn
    bal = cms.balance
    cred = rpc(cms, "credit_req", 12, sid=sid, txn="C1", amount=25000)
    assert cred["ok"] and cred["balance"] == bal + 25000
    cred = rpc(cms, "credit_req", 13, sid=sid, txn="C1", amount=25000)
    assert cred["ok"] and cred["balance"] == bal + 25000  # repeat: no double add
    assert cms.balance == bal + 25000
    # drop_credit swallows only the first credit (firmware retry path)
    cms2 = FakeCms(balance=1000, drop_credit=True)
    sid2 = rpc(cms2, "auth_req", 1, uid="CC DD")["sid"]
    assert rpc(cms2, "credit_req", 2, sid=sid2, txn="C2", amount=500) is None
    cred = rpc(cms2, "credit_req", 3, sid=sid2, txn="C2", amount=500)
    assert cred["ok"] and cms2.balance == 1500
    # JSON envelope survives UTF-8
    txt = json.dumps({"t": "menu_res", "d": {"items": cms.menu}}, ensure_ascii=False)
    assert "Кафе" in txt


def test_order_status():
    """The countdown ring's data: an estimate up front, the bar's promise after,
    and a resync that replays whatever is still open."""
    cms = FakeCms(balance=100000, points=100)
    sid = rpc(cms, "auth_req", 1, uid="AA BB")["sid"]

    # The menu carries the cached average per drink, so the terminal can show
    # a per-item time and the estimate is reproducible from it.
    items = rpc(cms, "menu_req", 2)["items"]
    assert all("avg_s" in m for m in items), items

    # Estimate = max of the ordered drinks' averages (parallel pours), not sum.
    order = rpc(cms, "order_req", 3, sid=sid, pay="cash",
                items=[{"drink": 1, "qty": 1}, {"drink": 4, "qty": 1}])
    assert order["ok"] and order["num"] == 1
    assert order["status"] == "received"
    assert order["eta_src"] == "est"
    assert order["eta_s"] == 180 and order["eta_total_s"] == 180, order

    # Accepting promises a time, which outranks the estimate.
    assert "accepted" in cms.bar_accept(1, 12)
    pushes = cms.drain_pushes()
    assert len(pushes) == 1 and pushes[0]["t"] == "order_push"
    d = pushes[0]["d"]
    assert d["ok"] and d["sid"] == sid and d["num"] == 1
    assert d["status"] == "accepted" and d["eta_src"] == "promised"
    assert d["eta_s"] == 720, d
    assert not cms.drain_pushes()  # drained exactly once

    # Resync: a count, then one order_push per open order.
    second = rpc(cms, "order_req", 4, sid=sid, pay="cash", items=[{"drink": 3, "qty": 1}])
    assert second["num"] == 2
    replies = cms.handle({"t": "orders_req", "id": 5, "d": {"sid": sid}})
    assert replies[0]["t"] == "orders_res" and replies[0]["d"]["n"] == 2
    assert [r["t"] for r in replies[1:]] == ["order_push", "order_push"]
    # Newest first, so a firmware that keeps only one keeps the latest.
    assert [r["d"]["num"] for r in replies[1:]] == [2, 1]

    # A fulfilled order drops out of the resync and carries no ETA.
    assert "fulfilled" in cms.bar_ready(1)
    assert cms.drain_pushes()[0]["d"].get("eta_s") is None
    replies = cms.handle({"t": "orders_req", "id": 6, "d": {"sid": sid}})
    assert replies[0]["d"]["n"] == 1
    assert [r["d"]["num"] for r in replies[1:]] == [2]

    # Cancelling refunds what was charged and tells the terminal the balance moved.
    before = cms.balance
    assert "cancelled" in cms.bar_cancel(2)
    assert cms.balance == before + 12000
    assert [p["t"] for p in cms.drain_pushes()] == ["order_push", "balance_push"]
    assert cms.handle({"t": "orders_req", "id": 7, "d": {"sid": sid}})[0]["d"]["n"] == 0

    # An unknown session is refused rather than leaking another card's orders.
    assert not rpc(cms, "orders_req", 8, sid="s-nope")["ok"]

    # Orders belong to the CARD, not the session they were placed in. Re-auth
    # mints a new sid, and the open order has to survive that -- it is how an
    # order outlives a terminal reboot. A different card sees none of it.
    cms2 = FakeCms(balance=100000)
    first = rpc(cms2, "auth_req", 1, uid="AA BB")["sid"]
    assert rpc(cms2, "order_req", 2, sid=first, pay="cash",
               items=[{"drink": 1, "qty": 1}])["ok"]
    again = rpc(cms2, "auth_req", 3, uid="AA BB")["sid"]
    assert again != first, "re-auth should mint a new sid"
    replies = cms2.handle({"t": "orders_req", "id": 4, "d": {"sid": again}})
    assert replies[0]["d"]["n"] == 1, "the order must survive a new session"
    # ...and be addressed to the session holding the card now, not the old one.
    assert replies[1]["d"]["sid"] == again, replies[1]
    other = rpc(cms2, "auth_req", 5, uid="CC DD")["sid"]
    assert cms2.handle({"t": "orders_req", "id": 6, "d": {"sid": other}})[0]["d"]["n"] == 0

    # Already-closed orders cannot be reopened, and nothing is pushed for them.
    assert "not open" in cms.bar_ready(1)
    assert not cms.drain_pushes()


if __name__ == "__main__":
    test_mux()
    test_egm()
    test_cms()
    test_order_status()
    print("OK: mux framing, EGM SAS subset, CMS flows, order status all pass")
