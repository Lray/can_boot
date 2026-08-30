#ifndef UDS_MSG_H
#define UDS_MSG_H

#include <stdint.h>

/**
 * Build a UDS positive response with an explicit destination capacity.
 *
 * @param out          First writable destination byte.
 * @param out_capacity Number of writable bytes available at out.
 * @param request_sid  The request SID; the response echoes it with bit 6 set.
 * @param extra_data   Optional payload appended after the response SID.
 * @param extra_len    Number of bytes in extra_data.
 * @return Total response length in bytes, or 0 when the destination is too
 *         small or a required pointer is NULL.
 */
uint16_t UDS_Msg_BuildPositiveResponseChecked(uint8_t *out,
                                              uint16_t out_capacity,
                                              uint8_t request_sid,
                                              const uint8_t *extra_data,
                                              uint16_t extra_len);

/**
 * Build a UDS negative response: {0x7F, original_sid, nrc}.
 *
 * @param out         First of three writable destination bytes.
 * @param original_sid The SID that was rejected.
 * @param nrc         Negative response code.
 * @return Always 3.
 * @pre out points to at least three writable bytes.
 */
void UDS_Msg_BuildNegativeResponse(uint8_t *out,
                                       uint8_t original_sid,
                                       uint8_t nrc);

/**
 * Encode one unsigned 16-bit value in UDS network byte order.
 *
 * @param data  First of two writable destination bytes.
 * @param value Value to encode, most significant byte first.
 * @pre data points to at least two writable bytes.
 */
void UDS_Msg_WriteBe16(uint8_t *data, uint16_t value);

/**
 * Decode one unsigned 32-bit value from UDS network byte order.
 *
 * @param data First of four readable bytes, most significant byte first.
 * @return The decoded unsigned value.
 * @pre data points to at least four readable bytes.
 */
uint32_t UDS_Msg_ReadBe32(const uint8_t *data);

/**
 * Encode one unsigned 32-bit value in UDS network byte order.
 *
 * @param data  First of four writable destination bytes.
 * @param value Value to encode, most significant byte first.
 * @pre data points to at least four writable bytes.
 */
void UDS_Msg_WriteBe32(uint8_t *data, uint32_t value);

#endif /* UDS_MSG_H */
