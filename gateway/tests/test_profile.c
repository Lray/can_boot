#include <assert.h>
#include <stdint.h>

#include "profile.h"

int main(void)
{
    assert(CAN_ID_HEARTBEAT(1u) == 0x701u);
    assert(CAN_ID_HEARTBEAT(CAN_NODE_ID_MAX) == 0x77Fu);
    assert(CAN_HEARTBEAT_STATE_ALIVE == 0x05u);
    assert(CAN_HEARTBEAT_PERIOD_MS == 1000u);
    assert(CAN_HEARTBEAT_TIMEOUT_MS == 3000u);
    assert(CAN_ID_UDS_REQUEST(1u) == 0x601u);
    assert(CAN_ID_UDS_RESPONSE(1u) == 0x581u);
    assert(CAN_ID_UDS_REQUEST(CAN_NODE_ID_MAX) == 0x67Fu);
    assert(CAN_ID_UDS_RESPONSE(CAN_NODE_ID_MAX) == 0x5FFu);
    assert(CAN_ID_MCU_ULOG(1u) == 0x681u);
    assert(CAN_ID_MCU_ULOG(CAN_NODE_ID_MAX) == 0x6FFu);
    assert(CAN_FUNC_ID == 0x7DFu);
    assert(CAN_BITRATE == 500000u);
    assert(ISOTP_BLOCK_SIZE == 8u);
    assert(ISOTP_STMIN_MS == 2u);
    assert(P2_SERVER_DEFAULT_MS == 50u);
    assert(P2_STAR_SERVER_DEFAULT_MS == 5000u);
    assert(P2_STAR_SERVER_WIRE_UNIT_MS == 10u);
    assert(P2_STAR_SERVER_DEFAULT_WIRE == 0x01F4u);
    assert(UDS_RESPONSE_PENDING_MAX_COUNT == 8u);
    assert(MCU_UPDATE_TOTAL_TIMEOUT_MS == 500000u);
    assert(MCU_UPDATE_REMOTE_WAIT_MS == 540000u);
    assert(SESSION_DEFAULT == 0x01u);
    assert(NRC_BUSY_REPEAT_REQUEST == 0x21u);
    assert(TRANSFER_BLOCK_PAYLOAD == 256u);
    assert(MCUBOOT_IMAGE_VERSION_SIZE_BYTES == 8u);
    assert(UDS_REQUEST_DOWNLOAD_REQUEST_LEN == 11u);
    assert(UDS_REQUEST_DOWNLOAD_RESPONSE_LEN == 4u);
    assert(ROUTINE_ID_ERASE_MEMORY == 0xFF00u);
    assert(DID_BOOT_VERSION == 0xF1F0u);
    assert(DID_APP_VERSION == 0xF1F1u);
    assert(DID_UPDATER_VERSION == 0xF1F2u);
    assert(DID_ACTIVE_SLOT == 0xF1F3u);
    assert(DID_CONFIRM_RESULT == 0xF1F4u);
    assert(DID_LSS_IDENTITY == 0xF1F5u);
    assert(UDS_ROUTINE_CONTROL_REQUEST_LEN == 4u);
    assert(UDS_ERASE_MEMORY_RESULT_LEN == 8u);
    assert(DOWNLOAD_PREPARATION_TIMEOUT_MS == 30000u);
    assert(DOWNLOAD_PREPARATION_POLL_MS == 250u);
    return 0;
}
