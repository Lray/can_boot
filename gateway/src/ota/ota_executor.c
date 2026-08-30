#include "ota_executor.h"
#include "ota_snapshot.h"

#include <stdio.h>
#include <string.h>

#include "profile.h"
#include "resume_transfer.h"
#include "security_access.h"
#include "shared/image_confirm_result.h"
#include "util.h"

#define OTA_DEFAULT_PRECHECK_DEADLINE_MS 5000u
#define OTA_DEFAULT_POST_RESET_DEADLINE_MS 30000u

static OtaState_t classify_post_reset(const OtaPackage_t *package,
                                      const EcuSnapshot_t *after,
                                      uint8_t target_slot)
{
    if (after->active_slot == target_slot &&
        mcuboot_image_version_equal(
            &after->app_version,
            &package->image_version) &&
        after->confirm_result == IMAGE_CONFIRM_RESULT_OK)
    {
        return OTA_STATE_CONFIRMED;
    }
    return OTA_STATE_POST_RESET_CHECKED;
}

OtaState_t ota_executor_run(const OtaExecutorConfig_t *config,
                            const OtaPackage_t *package,
                            OtaExecutorResult_t *result_out)
{
    uint32_t precheck_window = 0u;
    uint32_t post_reset_window = 0u;
    uint64_t start = 0u;
    uint64_t deadline = 0u;
    uint8_t target_slot = 0u;
    int rc = 0;

    if (result_out == NULL)
    {
        return OTA_STATE_PACKAGE_VALIDATED;
    }
    memset(result_out, 0, sizeof(*result_out));
    if (config == NULL || package == NULL ||
        !uds_client_is_ready(config->client) || config->signer == NULL ||
        config->reconnect == NULL)
    {
        return OTA_STATE_PACKAGE_VALIDATED;
    }
    precheck_window = config->precheck_deadline_ms != 0u ? config->precheck_deadline_ms
                                                         : OTA_DEFAULT_PRECHECK_DEADLINE_MS;
    post_reset_window = config->post_reset_deadline_ms != 0u
                            ? config->post_reset_deadline_ms
                            : OTA_DEFAULT_POST_RESET_DEADLINE_MS;

    start = util_monotonic_ms();
    rc = read_snapshot_twice(config->client, config->reconnect,
                             config->reconnect_ctx, start + precheck_window, 0,
                             start + precheck_window,
                             &result_out->before);
    if (rc != 0)
    {
        return OTA_STATE_PACKAGE_VALIDATED;
    }
    /* 0x10 03: DiagnosticSessionControl -> Extended Session. */
    rc = uds_enter_session(config->client, SESSION_EXTENDED);
    if (rc == 0)
    {
        /* 0x10 02: DiagnosticSessionControl -> Programming Session. */
        rc = uds_enter_session(config->client, SESSION_PROGRAMMING);
    }
    if (rc == 0)
    {
        /* 0x3E 00: TesterPresent with zero sub-function. */
        rc = uds_tester_present(config->client);
    }
    if (rc != 0)
    {
        return OTA_STATE_PACKAGE_VALIDATED;
    }

    rc = security_access_unlock(config->client, config->signer);
    if (rc != 0)
    {
        /* Diagnostic only: never emit the seed, token, or key material. */
        fprintf(stderr, "gateway-worker: security-access failed rc=%d nrc=0x%02X\n",
                rc, config->client->last_nrc);
        return OTA_STATE_SESSION_OPEN;
    }

    rc = resume_transfer_execute(config->client, package->image_sha256,
                                 package->image_size, package->image,
                                 &target_slot);
    if (rc != 0)
    {
        return OTA_STATE_AUTHORIZED;
    }

    /* 0x11 01: ECUReset -> hard reset. */
    rc = uds_ecu_reset_hard(config->client);
    if (rc != 0 && rc != UDS_ERR_TIMEOUT)
    {
        return OTA_STATE_TRANSFERRED;
    }
    start = util_monotonic_ms();
    deadline = start + post_reset_window;
    if (config->reconnect(config->reconnect_ctx, config->client) != 0)
    {
        return OTA_STATE_RESET_SENT_OR_RESPONSE_LOST;
    }
    rc = read_snapshot_twice(
        config->client, config->reconnect, config->reconnect_ctx,
        deadline, 1,
        start + (post_reset_window < OTA_SNAPSHOT_FIRST_POST_LIMIT_MS
                     ? post_reset_window
                     : OTA_SNAPSHOT_FIRST_POST_LIMIT_MS),
        &result_out->after);
    if (rc != 0)
    {
        return OTA_STATE_RECONNECTED;
    }
    return classify_post_reset(package, &result_out->after, target_slot);
}
