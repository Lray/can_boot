#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "boot_observation.h"
#include "factory_identity.h"
#include "image_confirm.h"
#include "shared/uds_protocol.h"
#include "uds_read_did.h"

uint8_t BootObservation_GetActiveSlot(void) { return 0U; }
bool BootObservation_GetRunningImageVersion(mcuboot_image_version_t *version_out)
{
    *version_out = (mcuboot_image_version_t){1U, 2U, 3U, 4U};
    return true;
}
bool FactoryIdentity_Read(factory_identity_t *identity)
{
    *identity = (factory_identity_t){1U, 2U, 3U, 4U};
    return true;
}
image_confirm_result_t ImageConfirm_GetLastStartupResult(void)
{
    return IMAGE_CONFIRM_RESULT_OK;
}

int main(void)
{
    static const uint16_t project_dids[] = {
        DID_BOOT_VERSION, DID_APP_VERSION, DID_UPDATER_VERSION,
        DID_ACTIVE_SLOT, DID_CONFIRM_RESULT, DID_LSS_IDENTITY,
    };
    uint8_t data[UDS_READ_DID_RESPONSE_MAX_SIZE] = {0};
    uds_read_did_response_t response = {data, sizeof(data), 0U};

    for (uint16_t index = 0U; index < 6U; index++)
    {
        assert(project_dids[index] == (uint16_t)(0xF1F0U + index));
        assert(UDS_ReadDid_Build(project_dids[index], &response) ==
               UDS_READ_DID_RESULT_OK);
        assert(data[0] == SID_READ_DATA_BY_IDENTIFIER_POS);
        assert(data[1] == 0xF1U && data[2] == (uint8_t)(0xF0U + index));
        assert(response.length > 3U);
    }
    for (uint16_t did = 0xF180U; did <= 0xF182U; did++)
    {
        assert(UDS_ReadDid_Build(did, &response) ==
               UDS_READ_DID_RESULT_OUT_OF_RANGE);
    }
    return 0;
}
