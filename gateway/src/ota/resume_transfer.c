#include "resume_transfer.h"

#include <stdio.h>

#include "profile.h"
#include "util.h"

static int resume_transfer_remaining_size(uint32_t image_size,
                                          uint32_t resume_offset,
                                          uint32_t *remaining_size_out)
{
    if (remaining_size_out == NULL || image_size == 0u ||
        resume_offset > image_size)
    {
        return RESUME_TRANSFER_ERR_INVALID_ARG;
    }
    if (resume_offset != 0u && resume_offset != image_size &&
        (resume_offset % META_CHECKPOINT_INTERVAL) != 0u)
    {
        return RESUME_TRANSFER_ERR_CHECKPOINT_ALIGNMENT;
    }
    *remaining_size_out = image_size - resume_offset;
    return 0;
}

static int wait_for_download_preparation(
    UdsClient *client,
    const uint8_t payload_id[PAYLOAD_ID_SIZE],
    uint32_t image_size)
{
    uint64_t deadline = util_monotonic_ms() + DOWNLOAD_PREPARATION_TIMEOUT_MS;
    bool ready = false;
    int rc = uds_prepare_download(client, payload_id, image_size);

    if (rc != 0)
    {
        return rc;
    }
    do
    {
        rc = uds_prepare_download_ready(client, &ready);
        if (rc != 0 || ready)
        {
            return rc;
        }
        if (util_monotonic_ms() >= deadline ||
            util_sleep_ms(DOWNLOAD_PREPARATION_POLL_MS) != 0)
        {
            return UDS_ERR_TIMEOUT;
        }
    } while (true);
}

int resume_transfer_execute(UdsClient *client,
                            const uint8_t payload_id[PAYLOAD_ID_SIZE],
                            uint32_t image_size,
                            const uint8_t *image,
                            uint8_t *target_slot_out)
{
    UdsDownloadResponse response = {0};
    uint32_t remaining_size = 0u;
    uint32_t offset = 0u;
    uint8_t block_sequence = 1u;
    int rc = 0;

    if (client == NULL || payload_id == NULL || target_slot_out == NULL || image == NULL)
    {
        return RESUME_TRANSFER_ERR_INVALID_ARG;
    }

    rc = wait_for_download_preparation(client, payload_id, image_size);
    if (rc != 0)
    {
        fprintf(stderr, "gateway-worker: prepare-download failed rc=%d nrc=0x%02X\n",
                rc, client->last_nrc);
        return rc;
    }

    rc = uds_request_download(client,
                              payload_id,
                              image_size,
                              &response);
    if (rc != 0)
    {
        fprintf(stderr, "gateway-worker: request-download failed rc=%d nrc=0x%02X\n",
                rc, client->last_nrc);
        return rc;
    }
    if (response.max_block_len != TRANSFER_MAX_BLOCK_LENGTH)
    {
        fprintf(stderr, "gateway-worker: request-download block length mismatch actual=%u expected=%u\n",
                response.max_block_len, TRANSFER_MAX_BLOCK_LENGTH);
        return RESUME_TRANSFER_ERR_BLOCK_PAYLOAD;
    }
    if (response.target_slot > OTA_SLOT_B)
    {
        fprintf(stderr, "gateway-worker: request-download invalid target slot=%u\n",
                response.target_slot);
        return RESUME_TRANSFER_ERR_IDENTITY_MISMATCH;
    }
    rc = resume_transfer_remaining_size(image_size,
                                        response.resume_offset,
                                        &remaining_size);
    if (rc != 0)
    {
        fprintf(stderr, "gateway-worker: request-download invalid resume offset=%lu rc=%d\n",
                (unsigned long)response.resume_offset, rc);
        return rc;
    }
    if (remaining_size == 0u)
    {
        *target_slot_out = response.target_slot;
        return 0;
    }

    offset = response.resume_offset;
    while (offset < image_size)
    {
        uint32_t remaining = image_size - offset;
        uint16_t chunk = (uint16_t)(remaining > TRANSFER_BLOCK_PAYLOAD
                                        ? TRANSFER_BLOCK_PAYLOAD
                                        : remaining);

        rc = uds_transfer_data(client, block_sequence, image + offset, chunk);
        if (rc != 0)
        {
            fprintf(stderr, "gateway-worker: transfer-data failed seq=%u rc=%d nrc=0x%02X\n",
                    block_sequence, rc, client->last_nrc);
            return rc;
        }
        offset += chunk;
        block_sequence++;
    }

    rc = uds_request_transfer_exit(client);
    if (rc != 0)
    {
        fprintf(stderr, "gateway-worker: transfer-exit failed rc=%d nrc=0x%02X\n",
                rc, client->last_nrc);
        return rc;
    }

    *target_slot_out = response.target_slot;
    return 0;
}
