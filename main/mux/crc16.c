#include "crc16.h"

uint16_t crc16_kermit_update(uint16_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);
        }
    }
    return crc;
}

uint16_t crc16_kermit(const uint8_t *data, size_t len)
{
    return crc16_kermit_update(0x0000, data, len);
}
