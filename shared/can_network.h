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

/* --- Classic CAN 11-bit identifiers --- */
/*
 * Project-specific single-MCU heartbeat, designed after CANopen Heartbeat.
 * This is not a complete CANopen NMT Heartbeat protocol.  The product keeps
 * the fixed 0x700 identifier because it has no configurable Node-ID.
 */
#define CAN_ID_HEARTBEAT 0x700U
#define CAN_HEARTBEAT_STATE_ALIVE 0x05U
#define CAN_HEARTBEAT_PERIOD_MS 1000U
/* Consumers decide online state from elapsed receive time, never counters. */
#define CAN_HEARTBEAT_TIMEOUT_MS 3000U
#define CAN_ID_UDS_REQUEST 0x7E0U
#define CAN_ID_UDS_RESPONSE 0x7E8U
/* Reserved proprietary broadcast from this MCU to the Gateway only. */
#define CAN_ID_MCU_ULOG 0x6D0U

/* --- ISO-TP product profile (encoded using the ISO-TP wire format) --- */
#define ISOTP_BLOCK_SIZE 8U
#define ISOTP_STMIN_MS 2U

#endif /* CAN_NETWORK_H */
