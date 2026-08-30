#include "log_frame.h"

#include <linux/can.h>
#include <string.h>

LogFrameDecodeResult_t log_frame_decode(
    const struct can_frame *raw_frame,
    EcuUlogFrame_t *decoded_frame)
{
    if (raw_frame == NULL || decoded_frame == NULL) {
        return LOG_FRAME_MALFORMED;
    }
    if (raw_frame->can_id != ECU_ULOG_CAN_ID) {
        return LOG_FRAME_OTHER_CAN_ID;
    }
    if (raw_frame->can_dlc < ECU_ULOG_FRAME_HEADER_SIZE ||
        raw_frame->can_dlc > CAN_MAX_DLEN) {
        return LOG_FRAME_MALFORMED;
    }

    (void)memset(decoded_frame, 0, sizeof(*decoded_frame));
    decoded_frame->control = raw_frame->data[0];
    decoded_frame->payload_length = (uint8_t)(
        raw_frame->can_dlc - ECU_ULOG_FRAME_HEADER_SIZE);
    if (decoded_frame->payload_length > 0u) {
        (void)memcpy(
            decoded_frame->payload,
            &raw_frame->data[ECU_ULOG_FRAME_HEADER_SIZE],
            decoded_frame->payload_length);
    }

    return LOG_FRAME_VALID;
}

uint8_t log_frame_fragment_index(const EcuUlogFrame_t *frame)
{
    if (frame == NULL) {
        return 0u;
    }

    return (uint8_t)(frame->control & ECU_ULOG_CONTROL_FRAGMENT_MASK);
}

bool log_frame_is_start(const EcuUlogFrame_t *frame)
{
    return frame != NULL &&
           (frame->control & ECU_ULOG_CONTROL_START) != 0u;
}

bool log_frame_is_end(const EcuUlogFrame_t *frame)
{
    return frame != NULL &&
           (frame->control & ECU_ULOG_CONTROL_END) != 0u;
}
