#include "uds_read_did.h"

#include "boot_observation.h"
#include "image_confirm.h"
#include "shared/uds_protocol.h"
#include "sysflash.h"
#include "uds_msg.h"

#include <stddef.h>
#include <string.h>

static void UDS_ReadDid_SetResponseHeader(
    uint16_t did,
    uds_read_did_response_t *response)
{
    response->data[0] = SID_READ_DATA_BY_IDENTIFIER_POS;
    UDS_Msg_WriteBe16(&response->data[1], did);
}

static void UDS_ReadDid_SetTextVersion(
    uds_read_did_response_t *response,
    uint8_t prefix)
{
    response->data[3] = prefix;
    response->data[4] = '0';
    response->data[5] = '0';
    response->data[6] = '1';
    response->length = 7U;
}

static uds_read_did_result_t UDS_ReadDid_BuildStandardResponse(
    uint16_t did,
    uds_read_did_response_t *response)
{
    mcuboot_image_version_t running_image_version = {0};
    uint8_t active_slot = SLOT_INVALID;

    UDS_ReadDid_SetResponseHeader(did, response);

    switch (did)
    {
        case DID_BOOT_VERSION:
            UDS_ReadDid_SetTextVersion(response, (uint8_t)'B');
            return UDS_READ_DID_RESULT_OK;

        case DID_APP_VERSION:
            if (!BootObservation_GetRunningImageVersion(
                    &running_image_version))
            {
                return UDS_READ_DID_RESULT_BUILD_FAILED;
            }

            response->data[3] = running_image_version.iv_major;
            response->data[4] = running_image_version.iv_minor;
            UDS_Msg_WriteBe16(
                &response->data[5],
                running_image_version.iv_revision);
            UDS_Msg_WriteBe32(
                &response->data[7],
                running_image_version.iv_build_num);
            response->length = 11U;
            return UDS_READ_DID_RESULT_OK;

        case DID_UPDATER_VERSION:
            UDS_ReadDid_SetTextVersion(response, (uint8_t)'U');
            return UDS_READ_DID_RESULT_OK;

        case DID_ACTIVE_SLOT:
            active_slot = BootObservation_GetActiveSlot();
            if (active_slot == SLOT_INVALID)
            {
                return UDS_READ_DID_RESULT_BUILD_FAILED;
            }
            response->data[3] = active_slot;
            response->length = 4U;
            return UDS_READ_DID_RESULT_OK;

        case DID_CONFIRM_RESULT:
            response->data[3] =
                (uint8_t)ImageConfirm_GetLastStartupResult();
            response->length = 4U;
            return UDS_READ_DID_RESULT_OK;

        default:
            return UDS_READ_DID_RESULT_OUT_OF_RANGE;
    }
}

uds_read_did_result_t UDS_ReadDid_Build(
    uint16_t did,
    uds_read_did_response_t *response)
{
    if ((response == NULL) || (response->data == NULL)
        || (response->capacity < UDS_READ_DID_RESPONSE_MAX_SIZE))
    {
        return UDS_READ_DID_RESULT_BUILD_FAILED;
    }

    response->length = 0U;
    (void)memset(response->data, 0, response->capacity);
    return UDS_ReadDid_BuildStandardResponse(did, response);
}
