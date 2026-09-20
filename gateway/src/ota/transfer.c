#include "transfer.h"

#include <stdio.h>

#include "profile.h"
#include "util.h"

static int wait_for_erase_completion(UdsClient *client)
{
    uint64_t deadline = util_monotonic_ms() + DOWNLOAD_PREPARATION_TIMEOUT_MS;
    bool complete = false;
    int rc = uds_erase_memory(client);

    if (rc != 0)
    {
        return rc;
    }
    do
    {
        uint64_t now_ms = util_monotonic_ms();

        rc = uds_erase_memory_results(client, &complete);
        if (rc == 0 && complete)
        {
            return 0;
        }
        /* RequestRoutineResults before results are ready returns NRC 0x24. */
        if (rc != 0 && !(rc == UDS_ERR_NEGATIVE_RESPONSE &&
                         client->last_nrc == NRC_REQUEST_SEQUENCE_ERROR))
        {
            return rc;
        }
        if (now_ms >= deadline ||
            util_sleep_ms(DOWNLOAD_PREPARATION_POLL_MS) != 0)
        {
            return UDS_ERR_TIMEOUT;
        }
    } while (true);
}

int transfer_execute(UdsClient *client,
                            uint32_t image_size,
                            const uint8_t *image)
{
    uint16_t max_block_len = 0u;
    uint16_t chunk_limit = 0u;
    uint32_t offset = 0u;
    uint8_t block_sequence = 1u;
    int rc = 0;

    if (client == NULL || image == NULL)
    {
        return TRANSFER_ERR_INVALID_ARG;
    }

    rc = wait_for_erase_completion(client);
    if (rc != 0)
    {
        fprintf(stderr, "mcu-update-engine: prepare-download failed rc=%d nrc=0x%02X\n",
                rc, client->last_nrc);
        return rc;
    }

    rc = uds_request_download(client,
                              image_size,
                              &max_block_len);
    if (rc != 0)
    {
        fprintf(stderr, "mcu-update-engine: request-download failed rc=%d nrc=0x%02X\n",
                rc, client->last_nrc);
        return rc;
    }
    if (max_block_len <= 2u)
    {
        return TRANSFER_ERR_BLOCK_PAYLOAD;
    }
    chunk_limit = (uint16_t)(max_block_len - 2u);
    if (chunk_limit > TRANSFER_BLOCK_PAYLOAD)
    {
        chunk_limit = TRANSFER_BLOCK_PAYLOAD;
    }

    while (offset < image_size)
    {
        uint32_t remaining = image_size - offset;
        uint16_t chunk = (uint16_t)(remaining > chunk_limit
                                        ? chunk_limit
                                        : remaining);

        rc = uds_transfer_data(client, block_sequence, image + offset, chunk);
        if (rc != 0)
        {
            fprintf(stderr, "mcu-update-engine: transfer-data failed seq=%u rc=%d nrc=0x%02X\n",
                    block_sequence, rc, client->last_nrc);
            return rc;
        }
        offset += chunk;
        block_sequence++;
    }

    rc = uds_request_transfer_exit(client);
    if (rc != 0)
    {
        fprintf(stderr, "mcu-update-engine: transfer-exit failed rc=%d nrc=0x%02X\n",
                rc, client->last_nrc);
        return rc;
    }

    return 0;
}
