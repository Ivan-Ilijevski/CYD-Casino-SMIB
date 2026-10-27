// SAS 6.03 subset: command/exception/AFT codes + BCD helpers.
// The SMIB is the SAS host (master); the slot game answers as the EGM.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define SAS_EGM_ADDR            0x01

// General poll (single byte, no CRC): the addressed poll is 0x80 | address.
// For our EGM at address 1 that is 0x81. There is no A/B alternation — the
// alternating 0x80/0x81 was gap G1 (a real EGM at address 1 ignores 0x80).
// The EGM responds with one exception byte; 0x00 = no activity.
#define SAS_GENERAL_POLL        (0x80 | SAS_EGM_ADDR)

// Long polls (framed [addr][cmd][body][crc16 lo][crc16 hi])
#define SAS_CMD_TOTAL_METERS    0x19  // coin-in/out, drop, jackpot, games (4-byte BCD each)
#define SAS_CMD_CURRENT_CREDITS 0x1A  // 4-byte BCD
#define SAS_CMD_MACHINE_ID      0x1F
#define SAS_CMD_SELECTED_METERS 0x2F  // TODO: not implemented; 0x19 covers the need
#define SAS_CMD_VERSION_SERIAL  0x54
#define SAS_CMD_AFT_TRANSFER    0x72
#define SAS_CMD_AFT_REGISTER    0x73

// Exception codes (general-poll responses)
#define SAS_EXC_NONE            0x00
#define SAS_EXC_DOOR_OPEN       0x11
#define SAS_EXC_AFT_COMPLETE    0x69
#define SAS_EXC_GAME_STARTED    0x7E
#define SAS_EXC_GAME_ENDED      0x7F

// AFT transfer codes (request byte 0 of the 0x72 body)
#define SAS_AFT_CODE_FULL       0x00  // full transfer request
#define SAS_AFT_CODE_INTERROGATE 0xFF

// AFT transfer types (request byte 3 of the 0x72 body)
#define SAS_AFT_TYPE_TO_EGM     0x00  // in-house cashable -> machine
#define SAS_AFT_TYPE_FROM_EGM   0x80  // in-house cashable, machine -> host

// AFT transfer status (response)
#define SAS_AFT_STATUS_FULL_OK  0x00
#define SAS_AFT_STATUS_PENDING  0x40
// >= 0x80 -> failed

// AFT registration status (0x73 interrogate response)
#define SAS_AFT_REG_REGISTERED  0x00
#define SAS_AFT_REG_READY       0x01
#define SAS_AFT_REG_NOT_REG     0x80

// Big-endian packed BCD (most significant byte first), as used by SAS meters
// and AFT amounts.
static inline void sas_u64_to_bcd(uint64_t v, uint8_t *out, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        out[i] = (uint8_t)((v % 10) | (((v / 10) % 10) << 4));
        v /= 100;
    }
}

static inline uint64_t sas_bcd_to_u64(const uint8_t *in, int n)
{
    uint64_t v = 0;
    for (int i = 0; i < n; i++) {
        v = v * 100 + ((in[i] >> 4) * 10) + (in[i] & 0x0F);
    }
    return v;
}
