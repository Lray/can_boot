#ifndef LOG_FRAME_H
#define LOG_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#include "shared/ecu_log_can.h"
#include "shared/can_network.h"

struct can_frame;

#define ECU_ULOG_CAN_ID CAN_ID_ECU_ULOG
#define ECU_ULOG_FRAME_HEADER_SIZE 1u
#define ECU_ULOG_FRAME_PAYLOAD_SIZE ECU_LOG_CAN_PAYLOAD_SIZE
#define ECU_ULOG_MAX_FRAGMENTS ECU_LOG_CAN_MAX_FRAGMENTS
#define ECU_ULOG_MAX_LOG_SIZE ECU_LOG_CAN_MAX_LOG_SIZE

#define ECU_ULOG_CONTROL_START ECU_LOG_CAN_START
#define ECU_ULOG_CONTROL_END ECU_LOG_CAN_END
#define ECU_ULOG_CONTROL_FRAGMENT_MASK ECU_LOG_CAN_FRAGMENT_MASK

/* One decoded MCU ULog raw-CAN frame; this type contains no sink or state. */
typedef struct {
    uint8_t control;
    uint8_t payload_length;
    uint8_t payload[ECU_ULOG_FRAME_PAYLOAD_SIZE];
} EcuUlogFrame_t;

typedef enum {
    LOG_FRAME_MALFORMED = -1,
    LOG_FRAME_OTHER_CAN_ID = 0,
    LOG_FRAME_VALID = 1
} LogFrameDecodeResult_t;

/* Decodes one raw SocketCAN frame without performing record reassembly. */
LogFrameDecodeResult_t log_frame_decode(
    const struct can_frame *raw_frame,
    EcuUlogFrame_t *decoded_frame);

/* Reads the protocol control bits from an already decoded frame. */
uint8_t log_frame_fragment_index(const EcuUlogFrame_t *frame);
bool log_frame_is_start(const EcuUlogFrame_t *frame);
bool log_frame_is_end(const EcuUlogFrame_t *frame);

#endif /* LOG_FRAME_H */