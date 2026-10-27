"""Fake CMS backend: one canned player, drink menu, debit/commit/rollback
with the idempotency contract the firmware relies on, and the order status /
countdown feed the terminal's ring draws from."""

import sys
import time

# avg_s mirrors the real CMS's menu_items.avg_prep_s: how long an order
# containing this drink has been taking. The terminal shows an estimate built
# from these before the bar has accepted anything.
DEFAULT_MENU = [
    dict(drink=1, name="Кафе",       price=8000,  points_price=50,  avail=True,  avg_s=180),
    dict(drink=2, name="Кока-Кола",  price=10000, points_price=80,  avail=True,  avg_s=120),
    dict(drink=3, name="Скопско",    price=12000, points_price=100, avail=True,  avg_s=150),
    dict(drink=4, name="Вода",       price=6000,  points_price=40,  avail=True,  avg_s=90),
    dict(drink=5, name="Виски",      price=25000, points_price=0,   avail=False, avg_s=240),
]

# What an unmeasured drink is assumed to take; matches DEFAULT_PREP_S server-side.
DEFAULT_PREP_S = 420


class FakeCms:
    def __init__(self, player_name="Иван Илијевски", balance=1250000, points=340,
                 uid=None, fail_auth=False, fail_debit=False, drop_commit=False,
                 drop_credit=False):
        self.player_name = player_name
        self.balance = balance
        self.points = points
        self.known_uid = uid          # None = accept any card
        self.fail_auth = fail_auth
        self.fail_debit = fail_debit
        self.drop_commit = drop_commit  # swallow the first commit (retry test)
        self.drop_credit = drop_credit  # swallow the first cashout credit
        self.menu = DEFAULT_MENU
        self.sessions = {}
        self._next_sid = 1
        self._next_order = 1
        # txn -> dict(amount=..., state='held'|'committed'|'rolledback')
        self.txns = {}
        # num -> order dict; see _new_order.
        self.orders = {}
        # Unsolicited frames waiting to go down the link. The real CMS writes
        # straight to the terminal's socket; here the transport drains this.
        self._pushes = []

    def log(self, msg):
        print(f"[CMS] {msg}")

    def drain_pushes(self):
        """Unsolicited frames to write, oldest first. Clears the queue."""
        out, self._pushes = self._pushes, []
        return out

    def handle(self, msg: dict):
        """Returns a list of reply dicts (already enveloped)."""
        t = msg.get("t")
        mid = msg.get("id", 0)
        d = msg.get("d", {}) or {}
        handler = getattr(self, f"_on_{t}", None)
        if handler is None:
            self.log(f"unhandled message '{t}'")
            return []
        return handler(mid, d)

    @staticmethod
    def _res(t, mid, ok=True, err=None, **fields):
        d = dict(ok=ok, **fields)
        if err:
            d["err"] = err
        return {"t": t, "id": mid, "d": d}

    # ------------------------------------------------------------ session

    def _on_hello(self, mid, d):
        self.log(f"hello from {d.get('dev')} fw {d.get('fw')}")
        return [self._res("hello_res", mid)]

    def _on_ping(self, mid, d):
        return [{"t": "pong", "id": mid, "d": {}}]

    def _on_auth_req(self, mid, d):
        uid = d.get("uid", "")
        if self.fail_auth or (self.known_uid and uid != self.known_uid):
            self.log(f"auth REJECTED for uid {uid}")
            return [self._res("auth_res", mid, ok=False,
                              err=dict(code="unknown_card", msg="Непозната картичка"))]
        sid = f"s-{self._next_sid}"
        self._next_sid += 1
        self.sessions[sid] = uid
        self.log(f"auth OK uid {uid} -> {sid}")
        return [self._res("auth_res", mid, sid=sid,
                          player=dict(name=self.player_name, balance=self.balance,
                                      points=self.points, cur="MKD"))]

    def _on_logout(self, mid, d):
        sid = d.get("sid")
        self.sessions.pop(sid, None)
        self.log(f"logout {sid} ({d.get('reason')})")
        return []

    # ------------------------------------------------------------ funds

    def _on_debit_req(self, mid, d):
        txn, amount = d.get("txn", ""), int(d.get("amount", 0))
        if self.fail_debit:
            return [self._res("debit_res", mid, ok=False, txn=txn,
                              err=dict(code="denied", msg="Одбиено од системот"))]
        if amount <= 0 or amount > self.balance:
            return [self._res("debit_res", mid, ok=False, txn=txn,
                              err=dict(code="insufficient", msg="Недоволно средства"))]
        self.balance -= amount
        self.txns[txn] = dict(amount=amount, state="held")
        self.log(f"debit hold {amount / 100:.2f} txn {txn}, balance {self.balance / 100:.2f}")
        return [self._res("debit_res", mid, txn=txn, balance=self.balance)]

    def _on_debit_commit(self, mid, d):
        txn = d.get("txn", "")
        if self.drop_commit:
            self.drop_commit = False
            self.log(f"DROPPING commit for {txn} (testing retry)")
            return []
        rec = self.txns.get(txn)
        if rec is None:
            return [self._res("debit_commit_res", mid, ok=False, txn=txn,
                              err=dict(code="unknown_txn", msg="Непозната трансакција"))]
        if rec["state"] == "rolledback":
            return [self._res("debit_commit_res", mid, ok=False, txn=txn,
                              err=dict(code="conflict", msg="Трансакцијата е поништена"))]
        rec["state"] = "committed"  # idempotent: repeat commits return ok
        self.log(f"commit {txn}")
        return [self._res("debit_commit_res", mid, txn=txn,
                          balance=self.balance, points=self.points)]

    def _on_debit_rollback(self, mid, d):
        txn = d.get("txn", "")
        rec = self.txns.get(txn)
        if rec is None:
            return [self._res("debit_rollback_res", mid, ok=False, txn=txn,
                              err=dict(code="unknown_txn", msg="Непозната трансакција"))]
        if rec["state"] == "held":
            rec["state"] = "rolledback"
            self.balance += rec["amount"]
            self.log(f"rollback {txn}, balance {self.balance / 100:.2f}")
        # idempotent: repeated rollbacks (or after commit-refusal) return ok
        return [self._res("debit_rollback_res", mid, txn=txn, balance=self.balance)]

    def _on_credit_req(self, mid, d):
        # Cashout: machine credits already moved off the EGM, deposit them on
        # the card. Idempotent by txn — the firmware retries until confirmed.
        txn, amount = d.get("txn", ""), int(d.get("amount", 0))
        if self.drop_credit:
            self.drop_credit = False
            self.log(f"DROPPING credit for {txn} (testing retry)")
            return []
        rec = self.txns.get(txn)
        if rec is None:
            self.balance += amount
            self.txns[txn] = dict(amount=amount, state="credited")
            self.log(f"CREDIT {amount / 100:.2f} txn {txn}, balance {self.balance / 100:.2f}")
        return [self._res("credit_res", mid, txn=txn,
                          balance=self.balance, points=self.points)]

    # ------------------------------------------------------------ drinks

    def _on_menu_req(self, mid, d):
        return [{"t": "menu_res", "id": mid, "d": dict(ok=True, items=self.menu)}]

    def _on_order_req(self, mid, d):
        items = d.get("items", [])
        pay = d.get("pay", "cash")
        total_cents = 0
        total_points = 0
        names = []
        by_id = {m["drink"]: m for m in self.menu}
        est_s = 0
        for it in items:
            m = by_id.get(it.get("drink"))
            qty = int(it.get("qty", 0))
            if not m or qty <= 0 or not m["avail"]:
                continue
            total_cents += m["price"] * qty
            total_points += m["points_price"] * qty
            names.append(f"{qty}x {m['name']}")
            # Max, not sum: drinks are poured in parallel, so the order is ready
            # when the slowest one is.
            est_s = max(est_s, m.get("avg_s") or DEFAULT_PREP_S)
        if not names:
            return [self._res("order_res", mid, ok=False,
                              err=dict(code="empty", msg="Празна нарачка"))]
        if pay == "points":
            if total_points > self.points:
                return [self._res("order_res", mid, ok=False,
                                  err=dict(code="insufficient", msg="Недоволно поени"))]
            self.points -= total_points
        else:
            if total_cents > self.balance:
                return [self._res("order_res", mid, ok=False,
                                  err=dict(code="insufficient", msg="Недоволно средства"))]
            self.balance -= total_cents
        num = self._next_order
        self._next_order += 1
        order_id = f"o-{num}"
        # Keyed by card (uid), not by the session it was placed in: the sid
        # changes on every re-auth, so a session-scoped order would vanish the
        # moment the player taps in again. Mirrors the real CMS, which resolves
        # orders through the card and pushes to whatever session holds it now.
        order = dict(num=num, id=order_id, uid=self.sessions.get(d.get("sid")),
                     status="received",
                     created=time.monotonic(), est_s=est_s or DEFAULT_PREP_S,
                     promised_at=None, pay=pay,
                     charge=total_points if pay == "points" else total_cents,
                     names=names)
        self.orders[num] = order
        self.log(f"ORDER #{num} {order_id}: {', '.join(names)} ({pay}) -> "
                 f"balance {self.balance / 100:.2f}, points {self.points}; "
                 f"estimate {order['est_s']}s")
        return [self._res("order_res", mid,
                          balance=self.balance, points=self.points,
                          **self._order_fields(order))]

    # ------------------------------------------------------------ order status

    def _order_fields(self, o):
        """The order half of order_res / order_push."""
        d = dict(order=o["id"], num=o["num"], status=o["status"])
        now = time.monotonic()
        if o["status"] == "accepted" and o["promised_at"] is not None:
            d["eta_s"] = max(0, int(round(o["promised_at"] - now)))
            d["eta_total_s"] = max(1, int(round(o["promised_at"] - o["created"])))
            d["eta_src"] = "promised"
        elif o["status"] in ("received", "accepted"):
            # Counted down from creation, so a long wait shrinks the estimate
            # instead of restarting it.
            elapsed = now - o["created"]
            d["eta_s"] = max(0, int(round(o["est_s"] - elapsed)))
            d["eta_total_s"] = o["est_s"]
            d["eta_src"] = "est"
        return d

    def _current_sid(self, uid):
        """The session holding this card right now, or None."""
        for sid, held in self.sessions.items():
            if held == uid:
                return sid
        return None

    def _live_orders(self, uid):
        """This card's still-open orders, newest first, like the real CMS."""
        live = [o for o in self.orders.values()
                if o["uid"] == uid and o["status"] in ("received", "accepted")]
        live.sort(key=lambda o: o["num"], reverse=True)
        return live[:4]

    def _push_order(self, o):
        # Addressed to whichever session holds the card now, not the one the
        # order was placed in. Nobody holding it means the push is dropped,
        # exactly as PushRegistry.send() does server-side -- the terminal
        # repairs that by re-asking on its next auth or link-up.
        sid = self._current_sid(o["uid"])
        if sid is None:
            self.log(f"order #{o['num']} changed but no live session holds the card")
            return
        self._pushes.append({"t": "order_push", "id": 0,
                             "d": dict(ok=True, sid=sid, **self._order_fields(o))})

    def _on_orders_req(self, mid, d):
        sid = d.get("sid")
        if sid not in self.sessions:
            return [self._res("orders_res", mid, ok=False,
                              err=dict(code="denied", msg="Сесијата е истечена"))]
        live = self._live_orders(self.sessions[sid])
        self.log(f"orders_req {sid} -> {len(live)} open")
        # A count, then one order_push per order: the firmware parses a single
        # order shape and never an array.
        out = [self._res("orders_res", mid, n=len(live))]
        for o in live:
            out.append({"t": "order_push", "id": 0,
                        "d": dict(ok=True, sid=sid, **self._order_fields(o))})
        return out

    # --- the bar's side, driven from the host sim console ---

    def bar_accept(self, num, minutes):
        o = self.orders.get(num)
        if not o or o["status"] != "received":
            return f"order #{num} is not waiting to be accepted"
        o["status"] = "accepted"
        o["promised_at"] = time.monotonic() + minutes * 60
        self._push_order(o)
        return f"order #{num} accepted, promised in {minutes} min"

    def bar_ready(self, num):
        o = self.orders.get(num)
        if not o or o["status"] not in ("received", "accepted"):
            return f"order #{num} is not open"
        o["status"] = "fulfilled"
        self._push_order(o)
        return f"order #{num} fulfilled"

    def bar_cancel(self, num):
        o = self.orders.get(num)
        if not o or o["status"] not in ("received", "accepted"):
            return f"order #{num} is not open"
        o["status"] = "cancelled"
        # Refund what was actually charged, like setOrderStatus does.
        if o["pay"] == "points":
            self.points += o["charge"]
        else:
            self.balance += o["charge"]
        self._push_order(o)
        self._pushes.append({"t": "balance_push", "id": 0,
                             "d": dict(ok=True, balance=self.balance, points=self.points)})
        return f"order #{num} cancelled and refunded"


# ---------------------------------------------------------------- bar console
# Lives here rather than in host_sim.py so cms_tcp_server.py can share it
# without pulling in pyserial.


def bar_command(cms, line):
    """Runs one console command, returning what to print (or None)."""
    parts = line.split()
    if not parts:
        return None
    if parts[0] == "orders":
        live = [o for o in cms.orders.values()
                if o["status"] in ("received", "accepted")]
        if not live:
            return "no open orders"
        return "\n".join(f"  #{o['num']} {o['status']:9} {', '.join(o['names'])}"
                         for o in sorted(live, key=lambda o: o["num"]))
    if parts[0] != "order" or len(parts) < 3:
        return f"unknown command {line!r}"
    action = parts[1]
    try:
        num = int(parts[2])
    except ValueError:
        return f"not an order number: {parts[2]!r}"
    if action == "accept":
        if len(parts) < 4:
            return "usage: order accept <n> <minutes>"
        try:
            minutes = int(parts[3])
        except ValueError:
            return f"not a minute count: {parts[3]!r}"
        return cms.bar_accept(num, minutes)
    if action == "ready":
        return cms.bar_ready(num)
    if action == "cancel":
        return cms.bar_cancel(num)
    return f"unknown order action {action!r}"


def console_loop(cms):
    """Reads bar commands off stdin. Daemon thread; the GIL is mutual exclusion
    enough for a bench tool that pokes a few dict fields."""
    for line in sys.stdin:
        try:
            out = bar_command(cms, line.strip())
        except Exception as exc:  # never take the bench down over a typo
            out = f"error: {exc}"
        if out:
            print(f"[BAR] {out}", flush=True)
