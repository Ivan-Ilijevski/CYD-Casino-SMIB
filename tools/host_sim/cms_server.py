"""Fake CMS backend: one canned player, drink menu, debit/commit/rollback
with the idempotency contract the firmware relies on."""

DEFAULT_MENU = [
    dict(drink=1, name="Кафе",       price=8000,  points_price=50,  avail=True),
    dict(drink=2, name="Кока-Кола",  price=10000, points_price=80,  avail=True),
    dict(drink=3, name="Скопско",    price=12000, points_price=100, avail=True),
    dict(drink=4, name="Вода",       price=6000,  points_price=40,  avail=True),
    dict(drink=5, name="Виски",      price=25000, points_price=0,   avail=False),
]


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

    def log(self, msg):
        print(f"[CMS] {msg}")

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
        for it in items:
            m = by_id.get(it.get("drink"))
            qty = int(it.get("qty", 0))
            if not m or qty <= 0 or not m["avail"]:
                continue
            total_cents += m["price"] * qty
            total_points += m["points_price"] * qty
            names.append(f"{qty}x {m['name']}")
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
        order_id = f"o-{self._next_order}"
        self._next_order += 1
        self.log(f"ORDER {order_id}: {', '.join(names)} ({pay}) -> "
                 f"balance {self.balance / 100:.2f}, points {self.points}")
        return [self._res("order_res", mid, order=order_id,
                          balance=self.balance, points=self.points)]
