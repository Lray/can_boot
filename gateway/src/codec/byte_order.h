#ifndef BYTE_ORDER_H
#define BYTE_ORDER_H

#include <stdint.h>

/** Decode a 32-bit unsigned integer encoded most-significant byte first. */
uint32_t byte_order_get_u32_be(const uint8_t data[4]);

/** Decode a 16-bit unsigned integer encoded most-significant byte first. */
uint16_t byte_order_get_u16_be(const uint8_t data[2]);

/** Encode a 32-bit unsigned integer with the most-significant byte first. */
void byte_order_put_u32_be(uint8_t data[4], uint32_t value);

/** Decode a 16-bit unsigned integer encoded least-significant byte first. */
uint16_t byte_order_get_u16_le(const uint8_t data[2]);

/** Decode a 32-bit unsigned integer encoded least-significant byte first. */
uint32_t byte_order_get_u32_le(const uint8_t data[4]);

#endif
