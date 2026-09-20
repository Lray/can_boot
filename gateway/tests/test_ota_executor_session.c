#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "ota_executor.h"
#include "profile.h"
#include "security_access.h"
#include "transfer.h"
#include "util.h"

static unsigned int s_session_count;
static unsigned int s_security_count;

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
    (void)reconnect_on_failure;
    (void)first_snapshot_deadline;
    memset(snapshot_out, 0, sizeof(*snapshot_out));
    snapshot_out->active_slot = OTA_SLOT_A;
    return 0;
}
int uds_enter_session(UdsClient *client, uint8_t session_type)
{
    (void)client;
    assert(session_type == SESSION_PROGRAMMING);
    s_session_count++;
    return 0;
}
int security_access_unlock(UdsClient *client, const TokenSignerClient_t *signer)
{
    (void)client;
    (void)signer;
    assert(s_session_count == 1u);
    s_security_count++;
    return UDS_ERR_NEGATIVE_RESPONSE;
}
int transfer_execute(UdsClient *client, uint32_t image_size, const uint8_t *image)
{
    (void)client;
    (void)image_size;
    (void)image;
    assert(0);
    return UDS_ERR_INVALID_ARG;
}
int uds_mcu_reset_hard(UdsClient *client)
{
    (void)client;
    assert(0);
    return UDS_ERR_INVALID_ARG;
}
static int reconnect_stub(void *ctx, UdsClient *client)
{
    (void)ctx;
    (void)client;
    assert(0);
    return UDS_ERR_INVALID_ARG;
}

int main(void)
{
    UdsClient client = {0};
    TokenSignerClient_t signer = {0};
    OtaPackage_t package = {0};
    OtaExecutorResult_t result;
    OtaExecutorConfig_t config = {0};

    config.client = &client;
    config.signer = &signer;
    config.reconnect = reconnect_stub;
    assert(ota_executor_run(&config, &package, &result) == OTA_STATE_SESSION_OPEN);
    assert(s_session_count == 1u && s_security_count == 1u);
    return 0;
}
