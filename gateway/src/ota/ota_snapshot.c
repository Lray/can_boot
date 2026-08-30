#include "ota_snapshot.h"

#include <time.h>

#include "byte_order.h"
#include "profile.h"
#include "util.h"

#define OTA_SNAPSHOT_MAX_SPAN_MS 500u
#define OTA_SNAPSHOT_INTERVAL_MS 1000u

static int read_snapshot(UdsClient *client, EcuSnapshot_t *snapshot_out)
{
    uint8_t value[8] = {0};
    size_t value_len = 0u;
    uint64_t start = util_monotonic_ms();
    int rc = 0;

    rc = uds_read_did(client, DID_ACTIVE_SLOT, value, sizeof(value),
                      &value_len);
    if (rc != 0 || value_len != 1u || value[0] > 1u)
    {
        return rc != 0 ? rc : UDS_ERR_MALFORMED_RESPONSE;
    }
    snapshot_out->active_slot = value[0];

    rc = uds_read_did(client, DID_APP_VERSION, value, sizeof(value),
                      &value_len);
    if (rc != 0 || value_len != MCUBOOT_IMAGE_VERSION_SIZE_BYTES)
    {
        return rc != 0 ? rc : UDS_ERR_MALFORMED_RESPONSE;
    }
    snapshot_out->app_version.iv_major = value[0];
    snapshot_out->app_version.iv_minor = value[1];
    snapshot_out->app_version.iv_revision = byte_order_get_u16_be(value + 2u);
    snapshot_out->app_version.iv_build_num = byte_order_get_u32_be(value + 4u);

    rc = uds_read_did(client, DID_CONFIRM_RESULT, value, sizeof(value),
                      &value_len);
    if (rc != 0 || value_len != 1u)
    {
        return rc != 0 ? rc : UDS_ERR_MALFORMED_RESPONSE;
    }
    snapshot_out->confirm_result = value[0];
    if (util_monotonic_ms() - start > OTA_SNAPSHOT_MAX_SPAN_MS)
    {
        return UDS_ERR_TIMEOUT;
    }
    return 0;
}

int read_snapshot_twice(UdsClient *client, UdsReconnectFn_t reconnect,
                        void *reconnect_ctx, uint64_t deadline,
                        int reconnect_on_failure,
                        uint64_t first_snapshot_deadline,
                        EcuSnapshot_t *snapshot_out)
{
    uint32_t retry_delay = 250u;

    while (util_monotonic_ms() < deadline)
    {
        EcuSnapshot_t first = {0};
        EcuSnapshot_t second = {0};
        int rc = read_snapshot(client, &first);

        if (rc == 0 && util_monotonic_ms() <= first_snapshot_deadline)
        {
            if (util_sleep_ms(OTA_SNAPSHOT_INTERVAL_MS) != 0)
            {
                return UDS_ERR_TIMEOUT;
            }
            rc = read_snapshot(client, &second);
            if (rc == 0 && first.active_slot == second.active_slot &&
                mcuboot_image_version_equal(&first.app_version,
                                            &second.app_version) &&
                first.confirm_result == second.confirm_result)
            {
                *snapshot_out = second;
                return 0;
            }
        }
        if (reconnect_on_failure && reconnect != NULL)
        {
            (void)reconnect(reconnect_ctx, client);
        }
        if (util_sleep_ms(retry_delay) != 0)
        {
            return UDS_ERR_TIMEOUT;
        }
        if (retry_delay < 1000u)
        {
            retry_delay *= 2u;
        }
    }
    return UDS_ERR_TIMEOUT;
}
