#include <assert.h>
#include <stdint.h>

#include "ota_executor.h"
#include "profile.h"
#include "security_access.h"
#include "shared/image_confirm_result.h"
#include "transfer.h"
#include "util.h"

static unsigned int s_session_count;
static unsigned int s_security_count;
static int s_security_result = UDS_NRC_SecurityAccessDenied;
static McuSnapshot_t s_before;
static McuSnapshot_t s_after;

uint64_t util_monotonic_ms(void) { return 0u; }
int uds_client_is_ready(const UdsClient *client) { return client != NULL; }
int read_snapshot_twice(UdsClient *client, UdsReconnectFn_t reconnect,
                        void *reconnect_ctx, uint64_t deadline,
                        int reconnect_on_failure, uint64_t first_snapshot_deadline,
                        McuSnapshot_t *snapshot_out)
{
    (void)client;
    (void)reconnect;
    (void)reconnect_ctx;
    (void)deadline;
    (void)first_snapshot_deadline;
    *snapshot_out = reconnect_on_failure ? s_after : s_before;
    return 0;
}
int uds_enter_session(UdsClient *client, uint8_t session_type)
{
    (void)client;
    assert(session_type == SESSION_PROGRAMMING);
    s_session_count++;
    return 0;
}
int security_access_unlock(UdsClient *client, SecurityTokenTee_t *token_tee)
{
    (void)client;
    (void)token_tee;
    assert(s_session_count == 1u);
    s_security_count++;
    return s_security_result;
}
int transfer_execute(UdsClient *client, uint32_t image_size, const uint8_t *image)
{
    (void)client;
    (void)image_size;
    (void)image;
    assert(s_security_result == 0);
    return 0;
}
int uds_mcu_reset_hard(UdsClient *client)
{
    (void)client;
    assert(s_security_result == 0);
    return 0;
}
static int reconnect_stub(void *ctx, UdsClient *client)
{
    (void)ctx;
    (void)client;
    assert(s_security_result == 0);
    return 0;
}

int main(void)
{
    UdsClient client = {0};
    OtaPackage_t package = {0};
    OtaExecutorResult_t result;
    OtaExecutorConfig_t config = {0};
    const factory_identity_t expected_identity = {1u, 2u, 3u, 4u};
    const factory_identity_t other_identities[] = {
        {9u, 2u, 3u, 4u}, {1u, 9u, 3u, 4u},
        {1u, 2u, 9u, 4u}, {1u, 2u, 3u, 9u},
    };

    config.client = &client;
    config.token_tee = (SecurityTokenTee_t *)&config;
    config.reconnect = reconnect_stub;
    package.identity = expected_identity;
    package.image_version = (mcuboot_image_version_t){1u, 2u, 3u, 4u};
    s_before.active_slot = OTA_SLOT_A;
    for (size_t i = 0u; i < sizeof(other_identities) / sizeof(other_identities[0]); ++i)
    {
        s_before.identity = other_identities[i];
        assert(ota_executor_run(&config, &package, &result) == OTA_STATE_PACKAGE_VALIDATED);
        assert(s_session_count == 0u && s_security_count == 0u);
    }
    s_before.identity = expected_identity;
    assert(ota_executor_run(&config, &package, &result) == OTA_STATE_SESSION_OPEN);
    assert(s_session_count == 1u && s_security_count == 1u);
    s_security_result = 0;
    s_after.identity = expected_identity;
    s_after.active_slot = OTA_SLOT_B;
    s_after.app_version = package.image_version;
    s_after.confirm_result = IMAGE_CONFIRM_RESULT_OK;
    s_session_count = s_security_count = 0u;
    assert(ota_executor_run(&config, &package, &result) == OTA_STATE_CONFIRMED);
    for (size_t i = 0u; i < sizeof(other_identities) / sizeof(other_identities[0]); ++i)
    {
        s_session_count = s_security_count = 0u;
        s_after.identity = other_identities[i];
        assert(ota_executor_run(&config, &package, &result) == OTA_STATE_POST_RESET_CHECKED);
    }
    return 0;
}
