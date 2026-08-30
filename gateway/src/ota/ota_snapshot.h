#ifndef OTA_SNAPSHOT_H
#define OTA_SNAPSHOT_H

#include <stdint.h>

#include "shared/mcuboot_image_version.h"
#include "uds_client.h"

/** First post-reset snapshot stability window cap in milliseconds. */
#define OTA_SNAPSHOT_FIRST_POST_LIMIT_MS 25000u

typedef struct
{
    uint8_t active_slot;
    mcuboot_image_version_t app_version;
    uint8_t confirm_result;
} EcuSnapshot_t;

/**
 * Poll the ECU snapshot DIDs until two consecutive reads agree or the deadline
 * passes.
 *
 * @param client Ready UDS client.
 * @param reconnect Optional transport-revival callback used between attempts.
 * @param reconnect_ctx Caller-owned context for the reconnect callback.
 * @param deadline Absolute monotonic deadline for the whole collection.
 * @param reconnect_on_failure Reconnect the ISO-TP channel between attempts.
 * @param first_snapshot_deadline Absolute deadline for the first stable pair.
 * @param snapshot_out Receives the stable snapshot on success.
 * @return 0 on success or a UDS_ERR_* value on failure.
 */
int read_snapshot_twice(UdsClient *client, UdsReconnectFn_t reconnect,
                        void *reconnect_ctx, uint64_t deadline,
                        int reconnect_on_failure,
                        uint64_t first_snapshot_deadline,
                        EcuSnapshot_t *snapshot_out);

#endif
