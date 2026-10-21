#include "uds_read_did.h"

#include "boot_observation.h"
#include "factory_identity.h"
#include "image_confirm.h"
#include "shared/uds_protocol.h"
#include "sysflash.h"

#include <stddef.h>
#include <string.h>

static void write_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void write_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static void set_text_version(uds_read_did_payload_t *payload, uint8_t prefix)
{
    payload->data[0] = prefix;
    payload->data[1] = '0';
    payload->data[2] = '0';
    payload->data[3] = '1';
    payload->length = 4U;
}

uds_read_did_result_t UDS_ReadDid_BuildPayload(
    uint16_t did,
    uds_read_did_payload_t *payload)
{
    mcuboot_image_version_t running_image_version = {0};
    factory_identity_t identity = {0};
    uint8_t active_slot;

    if (payload == NULL || payload->data == NULL ||
        payload->capacity < UDS_READ_DID_PAYLOAD_MAX_SIZE)
    {
        return UDS_READ_DID_RESULT_BUILD_FAILED;
    }
    payload->length = 0U;
    memset(payload->data, 0, payload->capacity);

    switch (did)
    {
        case DID_BOOT_VERSION:
            set_text_version(payload, (uint8_t)'B');
            return UDS_READ_DID_RESULT_OK;

        case DID_APP_VERSION:
            if (!BootObservation_GetRunningImageVersion(
                    &running_image_version))
            {
                return UDS_READ_DID_RESULT_BUILD_FAILED;
            }
            payload->data[0] = running_image_version.iv_major;
            payload->data[1] = running_image_version.iv_minor;
            write_be16(&payload->data[2], running_image_version.iv_revision);
            write_be32(&payload->data[4], running_image_version.iv_build_num);
            payload->length = 8U;
            return UDS_READ_DID_RESULT_OK;

        case DID_UPDATER_VERSION:
            set_text_version(payload, (uint8_t)'U');
            return UDS_READ_DID_RESULT_OK;

        case DID_ACTIVE_SLOT:
            active_slot = BootObservation_GetActiveSlot();
            if (active_slot == SLOT_INVALID)
            {
                return UDS_READ_DID_RESULT_BUILD_FAILED;
            }
            payload->data[0] = active_slot;
            payload->length = 1U;
            return UDS_READ_DID_RESULT_OK;

        case DID_CONFIRM_RESULT:
            payload->data[0] =
                (uint8_t)ImageConfirm_GetLastStartupResult();
            payload->length = 1U;
            return UDS_READ_DID_RESULT_OK;

        case DID_FACTORY_IDENTITY:
            if (!FactoryIdentity_Read(&identity))
            {
                return UDS_READ_DID_RESULT_BUILD_FAILED;
            }
            write_be32(&payload->data[0], identity.vendor_id);
            write_be32(&payload->data[4], identity.product_code);
            write_be32(&payload->data[8], identity.revision_number);
            write_be32(&payload->data[12], identity.serial_number);
            payload->length = 16U;
            return UDS_READ_DID_RESULT_OK;

        default:
            return UDS_READ_DID_RESULT_OUT_OF_RANGE;
    }
}
