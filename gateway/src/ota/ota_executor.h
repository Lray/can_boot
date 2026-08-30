#ifndef OTA_EXECUTOR_H
#define OTA_EXECUTOR_H

#include <stdint.h>

#include "ota_snapshot.h"
#include "package_metadata.h"
#include "token_signer_client.h"
#include "uds_client.h"

typedef enum
{
    OTA_STATE_PACKAGE_VALIDATED = 0,
    OTA_STATE_SESSION_OPEN = 1,
    OTA_STATE_AUTHORIZED = 2,
    OTA_STATE_TRANSFERRED = 3,
    OTA_STATE_RESET_SENT_OR_RESPONSE_LOST = 4,
    OTA_STATE_RECONNECTED = 5,
    OTA_STATE_POST_RESET_CHECKED = 6,
    OTA_STATE_CONFIRMED = 7,
} OtaState_t;

typedef struct
{
    UdsClient *client;
    UdsReconnectFn_t reconnect;
    void *reconnect_ctx;
    const TokenSignerClient_t *signer;
    uint32_t precheck_deadline_ms;
    uint32_t post_reset_deadline_ms;
} OtaExecutorConfig_t;

typedef struct
{
    EcuSnapshot_t before;
    EcuSnapshot_t after;
} OtaExecutorResult_t;

/**
 * Execute one validated package through the ECU OTA lifecycle.
 *
 * @return Terminal state; OTA_STATE_CONFIRMED on success, otherwise the
 *         furthest lifecycle step completed before failure.
 */
OtaState_t ota_executor_run(const OtaExecutorConfig_t *config,
                            const OtaPackage_t *package,
                            OtaExecutorResult_t *result_out);

#endif