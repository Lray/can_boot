#ifndef CAN_NETWORK_H
#define CAN_NETWORK_H

/*
 * Shared Classic CAN wire contract between the MCU firmware (can/) and the
 * Linux gateway (gateway/).  Single source of truth for frame identifiers and
 * the ISO-TP product profile; both trees include it and must not redefine
 * these macros locally.
 *
 * Naming follows the MCU tree (UPPER_SNAKE with U suffix).
 */

/* --- Classic CAN physical profile --- */
#define CAN_BIT_RATE_KBIT 500U

/* --- Classic CAN 11-bit identifiers --- */
/* LSS assigns the active Node-ID. Unconfigured nodes expose LSS only. */
#define CAN_NODE_ID_MIN 1U
#define CAN_NODE_ID_MAX 127U
/* Project heartbeat borrows CANopen's allocation, without implementing NMT. */
#define CAN_ID_HEARTBEAT(node_id) (0x700U + (node_id))
#define CAN_HEARTBEAT_STATE_ALIVE 0x05U
#define CAN_HEARTBEAT_PERIOD_MS 1000U
/* Consumers decide online state from elapsed receive time, never counters. */
#define CAN_HEARTBEAT_TIMEOUT_MS 3000U
/* Project ISO-TP allocation in the unused default SDO ranges. These frames
 * carry UDS, not SDO; enabling CANopen SDO on these ranges is prohibited. */
#define CAN_ID_UDS_REQUEST(node_id) (0x600U + (node_id))
#define CAN_ID_UDS_RESPONSE(node_id) (0x580U + (node_id))
/* Per-node proprietary log frames, disjoint from ISO-TP and heartbeat. */
#define CAN_ID_MCU_ULOG(node_id) (0x680U + (node_id))

/* --- ISO-TP product profile (encoded using the ISO-TP wire format) --- */
#define ISOTP_BLOCK_SIZE 8U
#define ISOTP_STMIN_MS 2U

#endif /* CAN_NETWORK_H */
