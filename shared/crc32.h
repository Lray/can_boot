#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>

/** Compute the ECU's reflected CRC-32/ISO-HDLC value. */
uint32_t Crc32_Compute(const uint8_t *data, size_t length);

#endif /* CRC32_H */
