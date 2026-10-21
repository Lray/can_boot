#ifndef UDS_PROTOCOL_H
#define UDS_PROTOCOL_H

#include "security_access_level.h"

/*
 * Shared UDS wire contract between the MCU firmware (can/) and the Linux
 * gateway (gateway/).  This header is the single source of truth for the
 * product-profile values used by both trees. Standard UDS state-machine
 * behavior and canonical NRC definitions come from the pinned iso14229 core.
 *
 * Naming follows the MCU tree (UPPER_SNAKE with U suffix).
 */

/* --- Session timing (ISO 14229) --- */
#define P2_SERVER_DEFAULT_MS 50U
#define P2_STAR_SERVER_DEFAULT_MS 5000U
/* ISO 14229 encodes P2*ServerMax in 10 ms units on the wire. */
#define P2_STAR_SERVER_DEFAULT_WIRE (P2_STAR_SERVER_DEFAULT_MS / 10U)

/* --- Service identifiers --- */
#define SID_DIAGNOSTIC_SESSION_CONTROL 0x10U
#define SID_MCU_RESET 0x11U
#define SID_READ_DATA_BY_IDENTIFIER 0x22U
#define SID_SECURITY_ACCESS 0x27U
#define SID_ROUTINE_CONTROL 0x31U
#define SID_REQUEST_DOWNLOAD 0x34U
#define SID_TRANSFER_DATA 0x36U
#define SID_REQUEST_TRANSFER_EXIT 0x37U
#define SID_TESTER_PRESENT 0x3EU
#define SID_POSITIVE_RESPONSE_MASK 0x40U
#define NEGATIVE_RESPONSE_SID 0x7FU
#define UDS_NEGATIVE_RESPONSE_LENGTH 3U
#define UDS_SUBFUNCTION_SUPPRESS_POSITIVE_RESPONSE_MASK 0x80U
#define UDS_SUBFUNCTION_VALUE_MASK 0x7FU

/* --- Positive response SIDs --- */
#define SID_DIAGNOSTIC_SESSION_CONTROL_POS 0x50U
#define SID_MCU_RESET_POS 0x51U
#define SID_READ_DATA_BY_IDENTIFIER_POS 0x62U
#define SID_SECURITY_ACCESS_POS 0x67U
#define SID_ROUTINE_CONTROL_POS 0x71U
#define SID_REQUEST_DOWNLOAD_POS 0x74U
#define SID_TRANSFER_DATA_POS 0x76U
#define SID_REQUEST_TRANSFER_EXIT_POS 0x77U
#define SID_TESTER_PRESENT_POS 0x7EU

/* --- Diagnostic sessions --- */
#define SESSION_DEFAULT 0x01U
#define SESSION_PROGRAMMING 0x02U
#define SUB_HARD_RESET 0x01U

/* --- SecurityAccess subfunction levels (from security_access_level.h) --- */

/* --- SecurityAccess seed challenge shape (can/security/security_seed.h) --- */
#define SECURITY_ACCESS_SEED_SIZE 4U

/* --- Download control --- */
#define DOWNLOAD_DATA_FORMAT_ID 0x00U
#define DOWNLOAD_ADDR_LEN_FORMAT_ID 0x44U
#define DOWNLOAD_MAX_BLOCK_LEN_FORMAT_ID 0x20U
#define DOWNLOAD_MEMORY_ADDRESS 0U
#define UDS_REQUEST_DOWNLOAD_REQUEST_LEN 11U

/* --- Erase memory routine (ISO 14229 routineIdentifier 0xFF00) --- */
#define ROUTINE_CONTROL_START 0x01U
#define ROUTINE_CONTROL_REQUEST_RESULTS 0x03U
#define ROUTINE_ID_ERASE_MEMORY 0xFF00U
/* 4-byte BE status record of the positive results response (ARDEP-aligned). */
#define ROUTINE_ERASE_RESULT_OK 0x00000000U
#define UDS_ROUTINE_CONTROL_REQUEST_LEN 4U
/* Positive results response: 71 03 FF 00 + 4-byte BE status record. */
#define UDS_ERASE_MEMORY_RESULT_LEN 8U

/* Complete positive response offsets include the 0x74 SID. */
#define UDS_REQUEST_DOWNLOAD_RESPONSE_LEN 4U
#define UDS_REQUEST_DOWNLOAD_RESPONSE_MAX_BLOCK_OFFSET 2U

/* --- Data identifiers --- */
#define DID_BOOT_VERSION 0xF1F0U
#define DID_APP_VERSION 0xF1F1U
#define DID_UPDATER_VERSION 0xF1F2U
#define DID_ACTIVE_SLOT 0xF1F3U
#define DID_CONFIRM_RESULT 0xF1F4U
#define DID_FACTORY_IDENTITY 0xF1F5U

#endif /* UDS_PROTOCOL_H */
