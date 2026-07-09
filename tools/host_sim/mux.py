"""Mux framing shared by the host simulator (and the future production
splitter app — import this module as the reference implementation).

Frame: [0x7E][chan][len_lo][len_hi][payload...][crc_lo][crc_hi]
CRC-16/KERMIT over chan+len+payload, appended LSB first. No byte stuffing:
parsers resync by scanning for 0x7E and validating chan/len sanity + CRC.
"""

CH_SAS = 0x01
CH_CMS = 0x02
CH_LOG = 0x7F
VALID_CHANNELS = {CH_SAS, CH_CMS, CH_LOG}
MAX_PAYLOAD = 2048
SYNC = 0x7E


def crc16_kermit(data: bytes, crc: int = 0x0000) -> int:
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc


def frame(chan: int, payload: bytes) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    body = bytes([chan, len(payload) & 0xFF, len(payload) >> 8]) + payload
    crc = crc16_kermit(body)
    return bytes([SYNC]) + body + bytes([crc & 0xFF, crc >> 8])


class MuxParser:
    """Incremental parser with the same resync semantics as the firmware:
    on any rejected candidate frame, rescan from the byte after its 0x7E."""

    def __init__(self):
        self._buf = bytearray()

    def feed(self, data: bytes):
        """Returns a list of (chan, payload) frames parsed so far."""
        self._buf.extend(data)
        frames = []
        while True:
            idx = self._buf.find(SYNC)
            if idx < 0:
                self._buf.clear()
                break
            if idx > 0:
                del self._buf[:idx]
            if len(self._buf) < 4:
                break
            chan = self._buf[1]
            length = self._buf[2] | (self._buf[3] << 8)
            if chan not in VALID_CHANNELS or length > MAX_PAYLOAD:
                del self._buf[:1]
                continue
            total = 4 + length + 2
            if len(self._buf) < total:
                break
            crc = crc16_kermit(bytes(self._buf[1:4 + length]))
            rx_crc = self._buf[4 + length] | (self._buf[5 + length] << 8)
            if crc != rx_crc:
                del self._buf[:1]
                continue
            frames.append((chan, bytes(self._buf[4:4 + length])))
            del self._buf[:total]
        return frames
