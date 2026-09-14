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
#define CAN_ID_HEARTBEAT 0x700U
#define CAN_ID_UDS_REQUEST 0x7E0U
#define CAN_ID_UDS_RESPONSE 0x7E8U
/* Reserved proprietary broadcast from this MCU to the Gateway only. */
#define CAN_ID_MCU_ULOG 0x6D0U

/* --- ISO-TP product profile (encoded using the ISO-TP wire format) --- */
#define ISOTP_BLOCK_SIZE 8U
#define ISOTP_STMIN_MS 2U

#endif /* CAN_NETWORK_H */
