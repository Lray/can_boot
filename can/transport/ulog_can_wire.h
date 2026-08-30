#ifndef ULOG_CAN_WIRE_H
#define ULOG_CAN_WIRE_H

#include <stdbool.h>
#include <stdint.h>

#include "can_frame.h"

/* Shared ECU log-over-CAN wire format. See shared/ecu_log_can.h. */
#include "../../shared/ecu_log_can.h"

/**
 * Build one raw-CAN fragment of a formatted ULog record.
 *
 * The function is side-effect free.  It lets a non-blocking producer queue a
 * complete record and lets a later poller submit exactly one fragment when the
 * CAN transmit FIFO has room.
 */
bool ULogCanWire_BuildFrame(
    const char *log,
    uint16_t log_length,
    uint8_t fragment_index,
    can_frame_t *frame);

#endif /* ULOG_CAN_WIRE_H */
