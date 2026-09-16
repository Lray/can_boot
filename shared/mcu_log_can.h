#ifndef MCU_LOG_CAN_H
#define MCU_LOG_CAN_H

#include <stdint.h>

#include "can_network.h"

/*
 * Shared raw MCU log-over-Classic-CAN wire format between the MCU firmware
 * (can/) and the Linux gateway (gateway/).  Single source of truth for the
 * fragment framing; both trees include it and must not redefine these
 * constants locally.
 *
 * Each frame uses CAN_ID_MCU_ULOG(node_id) and carries:
 *   data[0] : START (bit 7), END (bit 6), fragment index (bits 5..0)
 *   data[1..7] : up to seven UTF-8 log bytes
 *
 * The final frame has a DLC of 1 + remaining payload bytes.  This protocol is
 * deliberately independent of ISO-TP and UDS, so logging can never consume a
 * diagnostic receive path or wait for an FC frame.
 */

#define MCU_LOG_CAN_PAYLOAD_SIZE 7U
#define MCU_LOG_CAN_MAX_FRAGMENTS 64U
#define MCU_LOG_CAN_MAX_LOG_SIZE \
    (MCU_LOG_CAN_PAYLOAD_SIZE * MCU_LOG_CAN_MAX_FRAGMENTS)

#define MCU_LOG_CAN_START 0x80U
#define MCU_LOG_CAN_END 0x40U
#define MCU_LOG_CAN_FRAGMENT_MASK 0x3FU

#endif /* MCU_LOG_CAN_H */
