// CRC-16/KERMIT (reflected poly 0x8408, init 0x0000, no final XOR).
// Used for both the mux frame CRC and the SAS message CRC — same algorithm,
// different spans (mux: chan+len+payload; SAS: addr+cmd+data inside payload).
#pragma once

#include <stddef.h>
#include <stdint.h>

uint16_t crc16_kermit(const uint8_t *data, size_t len);

// Incremental variant: seed with 0x0000, feed chunks in order.
uint16_t crc16_kermit_update(uint16_t crc, const uint8_t *data, size_t len);
