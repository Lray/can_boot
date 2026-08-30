#include <assert.h>
#include <stdint.h>

#include "profile.h"

int main(void)
{
    assert(CAN_ID_UDS_REQUEST == 0x7E0u);
    assert(CAN_ID_UDS_RESPONSE == 0x7E8u);
    assert(CAN_FUNC_ID == 0x7DFu);
    assert(CAN_BITRATE == 500000u);
    assert(ISOTP_BLOCK_SIZE == 8u);
    assert(ISOTP_STMIN_MS == 2u);
    assert(P2_SERVER_DEFAULT_MS == 50u);
    assert(P2_STAR_SERVER_DEFAULT_MS == 5000u);
    assert(P2_STAR_SERVER_WIRE_UNIT_MS == 10u);
    assert(P2_STAR_SERVER_DEFAULT_WIRE == 0x01F4u);
    assert(UDS_RESPONSE_PENDING_REPEAT_INTERVAL_MS == 1500u);
    assert(UDS_RESPONSE_PENDING_MAX_COUNT == 8u);
    assert(SESSION_DEFAULT == 0x01u);
    assert(NRC_BUSY_REPEAT_REQUEST == 0x21u);
    assert(TESTER_PRESENT_MS == 1000u);
    assert(TRANSFER_BLOCK_PAYLOAD == 256u);
    assert(TRANSFER_MAX_BLOCK_LENGTH == 258u);
    assert(META_CHECKPOINT_INTERVAL == 8192u);
    assert(MCUBOOT_IMAGE_VERSION_SIZE_BYTES == 8u);
    assert(UDS_REQUEST_DOWNLOAD_REQUEST_LEN == 43u);
    assert(UDS_REQUEST_DOWNLOAD_RESPONSE_LEN == 9u);
    assert(ROUTINE_ID_PREPARE_DOWNLOAD == 0xF001u);
    assert(UDS_PREPARE_DOWNLOAD_ROUTINE_REQUEST_LEN == 40u);
    assert(UDS_PREPARE_DOWNLOAD_ROUTINE_RESULT_LEN == 5u);
    assert(DOWNLOAD_PREPARATION_TIMEOUT_MS == 30000u);
    assert(DOWNLOAD_PREPARATION_POLL_MS == 250u);
    return 0;
}
