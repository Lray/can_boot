#ifndef LOG_FRAME_H
#define LOG_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#include "shared/mcu_log_can.h"
#include "shared/can_network.h"

struct can_frame;

#define MCU_ULOG_FRAME_HEADER_SIZE 1u
#define MCU_ULOG_FRAME_PAYLOAD_SIZE MCU_LOG_CAN_PAYLOAD_SIZE
#define MCU_ULOG_MAX_FRAGMENTS MCU_LOG_CAN_MAX_FRAGMENTS
#define MCU_ULOG_MAX_LOG_SIZE MCU_LOG_CAN_MAX_LOG_SIZE

#define MCU_ULOG_CONTROL_START MCU_LOG_CAN_START
#define MCU_ULOG_CONTROL_END MCU_LOG_CAN_END
#define MCU_ULOG_CONTROL_FRAGMENT_MASK MCU_LOG_CAN_FRAGMENT_MASK

/* One decoded MCU ULog raw-CAN frame; this type contains no sink or state. */
typedef struct {
    uint8_t control;
    uint8_t payload_length;
    uint8_t payload[MCU_ULOG_FRAME_PAYLOAD_SIZE];
} McuUlogFrame_t;

typedef enum {
    LOG_FRAME_MALFORMED = -1,
    LOG_FRAME_OTHER_CAN_ID = 0,
    LOG_FRAME_VALID = 1
} LogFrameDecodeResult_t;

/* Decodes one raw SocketCAN frame without performing record reassembly. */
LogFrameDecodeResult_t log_frame_decode(
    const struct can_frame *raw_frame,
    uint32_t expected_can_id,
    McuUlogFrame_t *decoded_frame);

/* Reads the protocol control bits from an already decoded frame. */
uint8_t log_frame_fragment_index(const McuUlogFrame_t *frame);
bool log_frame_is_start(const McuUlogFrame_t *frame);
bool log_frame_is_end(const McuUlogFrame_t *frame);

#endif /* LOG_FRAME_H */
