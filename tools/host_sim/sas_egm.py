"""Fake EGM (gaming machine) answering the SMIB's SAS polls.

Implements the same subset the firmware speaks: general polls 0x80/0x81,
long polls 0x19/0x1A/0x1F/0x54/0x73 and AFT 0x72 (full transfer +
interrogate). Amounts are integer cents, BCD-encoded big-endian per SAS.
"""

from mux import crc16_kermit

ADDR = 0x01

EXC_NONE = 0x00
EXC_AFT_COMPLETE = 0x69

AFT_OK = 0x00
AFT_PENDING = 0x40


def u64_to_bcd(value: int, n: int) -> bytes:
    digits = str(value).rjust(n * 2, "0")[-n * 2:]
    return bytes((int(digits[i]) << 4) | int(digits[i + 1]) for i in range(0, n * 2, 2))


def bcd_to_u64(data: bytes) -> int:
    return int("".join(f"{b >> 4}{b & 0xF}" for b in data))


def with_crc(body: bytes) -> bytes:
    crc = crc16_kermit(body)
    return body + bytes([crc & 0xFF, crc >> 8])


class FakeEgm:
    def __init__(self, aft_status=AFT_OK, aft_pending_polls=0, verbose=False,
                 denom_code=0x01):
        self.credits = 0                      # cents on the machine
        self.asset = 0x00001234
        self.serial = "SIM0001"
        self.game_id = "SC"
        self.denom_code = denom_code          # reported in 0x1F (G3 testing)
        self.meters = dict(coin_in=125000, coin_out=93000, drop=32000,
                           jackpot=0, games=421)
        self.exceptions = []                  # queued for general polls
        self.forced_aft_status = aft_status   # what a transfer request returns
        self.aft_pending_polls = aft_pending_polls
        self._pending_left = 0
        self._last_aft = None                 # (status, amount, txn, ttype)
        self.verbose = verbose

    def log(self, msg):
        if self.verbose:
            print(f"[EGM] {msg}")

    def handle(self, data: bytes):
        """Returns the response bytes for one SAS message, or None."""
        if len(data) == 1 and data[0] in (0x80, 0x81):
            # G1: the addressed general poll is 0x81 (0x80|addr). A real EGM at
            # address 1 ignores a bare 0x80; we still answer it but warn, so a
            # host that wrongly alternates 0x80/0x81 is easy to spot.
            if data[0] == 0x80:
                self.log("general poll 0x80 (unaddressed) — expected 0x81")
            exc = self.exceptions.pop(0) if self.exceptions else EXC_NONE
            if exc != EXC_NONE:
                self.log(f"general poll -> exception {exc:02X}")
            return bytes([exc])

        if len(data) < 4 or data[0] != ADDR:
            self.log(f"ignoring frame {data.hex()}")
            return None
        crc = data[-2] | (data[-1] << 8)
        if crc16_kermit(data[:-2]) != crc:
            self.log(f"bad CRC on {data.hex()}")
            return None

        cmd = data[1]
        body = data[2:-2]
        if cmd == 0x19:
            m = self.meters
            payload = b"".join(u64_to_bcd(m[k], 4) for k in
                               ("coin_in", "coin_out", "drop", "jackpot", "games"))
            return with_crc(bytes([ADDR, cmd]) + payload)
        if cmd == 0x1A:
            return with_crc(bytes([ADDR, cmd]) + u64_to_bcd(self.credits, 4))
        if cmd == 0x1F:
            payload = (self.game_id.encode() + b"001"
                       + bytes([self.denom_code, 0x05, 0x00])
                       + b"\x00\x00" + b"PT0001" + b"9500")
            return with_crc(bytes([ADDR, cmd]) + payload)
        if cmd == 0x54:
            info = b"603" + self.serial.encode()
            return with_crc(bytes([ADDR, cmd, len(info)]) + info)
        if cmd == 0x73:
            payload = (bytes([0x01])                       # status: registration ready
                       + self.asset.to_bytes(4, "little")
                       + bytes(20) + bytes(4))
            return with_crc(bytes([ADDR, cmd, len(payload)]) + payload)
        if cmd == 0x72:
            return self._handle_aft(body)

        self.log(f"unsupported long poll {cmd:02X}")
        return None

    def _handle_aft(self, body: bytes):
        # body[0] = length, body[1] = transfer code
        if len(body) < 2:
            return None
        code = body[1]
        if code == 0xFF:  # interrogate
            if self._pending_left > 0:
                self._pending_left -= 1
                if self._pending_left == 0 and self._last_aft:
                    self._complete_pending()
            return self._aft_response()

        # Full transfer request: type at body[3] (0x00 = to EGM, 0x80 = from
        # EGM), cashable BCD5 at offset 4, txn after key.
        ttype = body[3]
        amount = bcd_to_u64(body[4:9])
        txn_len_off = 4 + 15 + 1 + 4 + 20
        txn_len = body[txn_len_off]
        txn = body[txn_len_off + 1:txn_len_off + 1 + txn_len].decode(errors="replace")

        if self.forced_aft_status not in (AFT_OK, AFT_PENDING):
            self._last_aft = (self.forced_aft_status, amount, txn, ttype)
            self.log(f"AFT {amount} cents txn {txn} -> FAIL {self.forced_aft_status:02X}")
        elif ttype == 0x80 and amount > self.credits:
            self._last_aft = (0x80, amount, txn, ttype)
            self.log(f"AFT -{amount} cents txn {txn} -> FAIL, only {self.credits} on machine")
        elif self.aft_pending_polls > 0:
            self._pending_left = self.aft_pending_polls
            self._last_aft = (AFT_PENDING, amount, txn, ttype)
            self.log(f"AFT {amount} cents txn {txn} -> pending x{self._pending_left}")
        else:
            self._last_aft = (AFT_OK, amount, txn, ttype)
            self._apply_transfer(amount, txn, ttype)
        return self._aft_response()

    def _complete_pending(self):
        status, amount, txn, ttype = self._last_aft
        self._last_aft = (AFT_OK, amount, txn, ttype)
        self._apply_transfer(amount, txn, ttype)

    def _apply_transfer(self, amount, txn, ttype):
        self.credits += -amount if ttype == 0x80 else amount
        self.exceptions.append(EXC_AFT_COMPLETE)
        sign = "-" if ttype == 0x80 else "+"
        print(f"[EGM] {sign}{amount / 100:.2f} credits (txn {txn}) -> machine total "
              f"{self.credits / 100:.2f}")

    def _aft_response(self):
        status, amount, txn, ttype = self._last_aft or (0x80, 0, "", 0x00)
        payload = bytearray()
        payload.append(0x00)                      # buffer position
        payload.append(status)                    # transfer status
        payload.append(0x00)                      # receipt status
        payload.append(ttype)                     # transfer type
        payload += u64_to_bcd(amount, 5)          # cashable
        payload += bytes(5) + bytes(5)            # restricted / nonrestricted
        payload.append(0x00)                      # flags
        payload += self.asset.to_bytes(4, "little")
        payload.append(len(txn))
        payload += txn.encode()
        payload += bytes(4) + bytes(3)            # date + time BCD
        return with_crc(bytes([ADDR, 0x72, len(payload)]) + bytes(payload))
