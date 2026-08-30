#ifndef PROFILE_H
#define PROFILE_H

#include <stdint.h>

#include "shared/uds_protocol.h"
#include "shared/can_network.h"
#include "shared/mcuboot_image_version.h"
#include "shared/security_token_profile.h"

#define CAN_FUNC_ID 0x7DFu
#define CAN_BITRATE 500000u

/* ISO-TP flow-control values from the shared can_network.h contract. */

/* Client-side UDS timing and framing (not part of the ECU wire contract). */
#define P2_STAR_SERVER_WIRE_UNIT_MS 10u
#define UDS_MAX_TRANSACTION_MS 30000u
#define UDS_ISOTP_TX_MARGIN_MS 3000u
#define TESTER_PRESENT_MS 1000u
#define DOWNLOAD_PREPARATION_TIMEOUT_MS 30000u
#define DOWNLOAD_PREPARATION_POLL_MS 250u
#define UDS_SESSION_CONTROL_RESPONSE_LENGTH 6u

#define TRANSFER_BLOCK_PAYLOAD 256u
#define TRANSFER_MAX_BLOCK_LENGTH 258u
#define META_CHECKPOINT_INTERVAL 8192u

#define SECURITY_ACCESS_SEED_MAX_SIZE 64u
#define SECURITY_ACCESS_TOKEN_MAX_SIZE SECURITY_TOKEN_MAX_SIZE

#define PACKAGE_SHA256_SIZE 32u

#define DEFAULT_SLOT_SIZE 0x00020000u

#define OTA_SLOT_A 0u
#define OTA_SLOT_B 1u

#endif
