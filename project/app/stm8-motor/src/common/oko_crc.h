#ifndef OKO_CRC_H
#define OKO_CRC_H

#include <stdint.h>

uint16_t oko_crc16(const uint8_t *data, uint16_t len);
uint32_t oko_crc32(const uint8_t *data, uint32_t len);

#endif /* OKO_CRC_H */