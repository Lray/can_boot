#include "download.h"

#include "boot_observation.h"
#include "erase_job.h"
#include "flash_map.h"
#include "sysflash.h"

#include <stddef.h>
#include <string.h>

typedef enum
{
    DOWNLOAD_STATE_IDLE = 0,
    DOWNLOAD_STATE_PREPARING,
    DOWNLOAD_STATE_READY,
    DOWNLOAD_STATE_TRANSFERRING,
    DOWNLOAD_STATE_COMPLETE,
    DOWNLOAD_STATE_PREPARATION_FAILED,
} download_state_t;

typedef struct
{
    download_state_t state;
    uint8_t target_slot;
    uint8_t next_block_sequence_counter;
    uint16_t previous_block_length;
    uint32_t received;
    uint32_t image_size;
} download_session_t;

static download_session_t s_download;

static void Download_Reset(void)
{
    (void)memset(&s_download, 0, sizeof(s_download));
    s_download.state = DOWNLOAD_STATE_IDLE;
    s_download.target_slot = SLOT_INVALID;
    s_download.next_block_sequence_counter = 0x01U;
}

void Download_Init(void)
{
    Download_Abort();
}

void Download_Abort(void)
{
    EraseJob_Reset();
    Download_Reset();
}

download_result_t Download_Prepare(void)
{
    if ((s_download.state != DOWNLOAD_STATE_IDLE) &&
        (s_download.state != DOWNLOAD_STATE_PREPARATION_FAILED))
    {
        return DOWNLOAD_RESULT_SEQUENCE_ERROR;
    }

    Download_Reset();
    s_download.target_slot = BootObservation_GetInactiveSlot();
    if (s_download.target_slot == SLOT_INVALID)
    {
        return DOWNLOAD_RESULT_REJECTED;
    }

    if (EraseJob_Start(s_download.target_slot) != 0)
    {
        Download_Reset();
        return DOWNLOAD_RESULT_PROGRAMMING_FAILURE;
    }

    s_download.state = DOWNLOAD_STATE_PREPARING;
    return DOWNLOAD_RESULT_OK;
}

static void Download_CompletePreparation(void)
{
    if (s_download.target_slot != BootObservation_GetInactiveSlot())
    {
        Download_Reset();
        s_download.state = DOWNLOAD_STATE_PREPARATION_FAILED;
        return;
    }

    s_download.state = DOWNLOAD_STATE_READY;
}

void Download_Poll(void)
{
    erase_job_status_t status;

    if (s_download.state != DOWNLOAD_STATE_PREPARING)
    {
        return;
    }

    status = EraseJob_Poll();
    if (status == ERASE_JOB_STATUS_PENDING)
    {
        return;
    }
    if (status == ERASE_JOB_STATUS_COMPLETE)
    {
        Download_CompletePreparation();
        return;
    }

    Download_Reset();
    s_download.state = DOWNLOAD_STATE_PREPARATION_FAILED;
}

download_preparation_status_t Download_GetPreparationStatus(void)
{
    switch (s_download.state)
    {
        case DOWNLOAD_STATE_PREPARING:
            return DOWNLOAD_PREPARATION_PENDING;

        case DOWNLOAD_STATE_READY:
        case DOWNLOAD_STATE_TRANSFERRING:
        case DOWNLOAD_STATE_COMPLETE:
            return DOWNLOAD_PREPARATION_READY;

        case DOWNLOAD_STATE_PREPARATION_FAILED:
            return DOWNLOAD_PREPARATION_FAILED;

        case DOWNLOAD_STATE_IDLE:
        default:
            return DOWNLOAD_PREPARATION_IDLE;
    }
}

download_result_t Download_Begin(uint32_t image_size)
{
    const struct flash_area *area = NULL;
    int area_id = -1;

    if (image_size == 0U)
    {
        return DOWNLOAD_RESULT_OUT_OF_RANGE;
    }
    if (s_download.state == DOWNLOAD_STATE_PREPARATION_FAILED)
    {
        return DOWNLOAD_RESULT_PROGRAMMING_FAILURE;
    }
    if (s_download.state == DOWNLOAD_STATE_COMPLETE)
    {
        return DOWNLOAD_RESULT_SEQUENCE_ERROR;
    }
    if (s_download.state != DOWNLOAD_STATE_READY)
    {
        return DOWNLOAD_RESULT_NOT_READY;
    }

    area_id = flash_area_id_from_multi_image_slot(0, (int)s_download.target_slot);
    if ((area_id < 0) || (flash_area_open((uint8_t)area_id, &area) != 0))
    {
        return DOWNLOAD_RESULT_OUT_OF_RANGE;
    }
    if (image_size > flash_area_get_size(area))
    {
        flash_area_close(area);
        return DOWNLOAD_RESULT_OUT_OF_RANGE;
    }
    flash_area_close(area);

    s_download.image_size = image_size;
    s_download.state = DOWNLOAD_STATE_TRANSFERRING;
    return DOWNLOAD_RESULT_OK;
}

download_result_t Download_Transfer(uint8_t block_sequence_counter,
                                    const uint8_t *payload,
                                    uint16_t length)
{
    const struct flash_area *area = NULL;
    uint32_t remaining = 0U;
    uint32_t next_received = 0U;
    uint8_t previous[DOWNLOAD_MAX_TRANSFER_PAYLOAD];
    int area_id = -1;

    if (s_download.state != DOWNLOAD_STATE_TRANSFERRING)
    {
        return DOWNLOAD_RESULT_SEQUENCE_ERROR;
    }
    if ((payload == NULL) || (length == 0U) ||
        (length > DOWNLOAD_MAX_TRANSFER_PAYLOAD))
    {
        return DOWNLOAD_RESULT_INCORRECT_LENGTH;
    }
    /* 仅放行期望 BSC；有上一块记录时，也放行上一 BSC 供后续校验。 */
    if (block_sequence_counter != s_download.next_block_sequence_counter &&
        (s_download.previous_block_length == 0U ||
         block_sequence_counter !=
             (uint8_t)(s_download.next_block_sequence_counter - 1U)))
    {
        return DOWNLOAD_RESULT_WRONG_BLOCK_SEQUENCE;
    }

    if (block_sequence_counter == s_download.next_block_sequence_counter)
    {
        if (s_download.received == s_download.image_size)
        {
            return DOWNLOAD_RESULT_SEQUENCE_ERROR;
        }
        remaining = s_download.image_size - s_download.received;
        if (length > remaining)
        {
            return DOWNLOAD_RESULT_TRANSFER_SUSPENDED;
        }
    }
    else if (length != s_download.previous_block_length)
    {
        return DOWNLOAD_RESULT_WRONG_BLOCK_SEQUENCE;
    }

    area_id = flash_area_id_from_multi_image_slot(0, (int)s_download.target_slot);
    if ((area_id < 0) || (flash_area_open((uint8_t)area_id, &area) != 0))
    {
        return DOWNLOAD_RESULT_OUT_OF_RANGE;
    }
    if (block_sequence_counter != s_download.next_block_sequence_counter)
    {
        int read_result = flash_area_read(area, s_download.received - length,
                                          previous, length);
        flash_area_close(area);
        if (read_result != 0)
        {
            return DOWNLOAD_RESULT_PROGRAMMING_FAILURE;
        }
        return memcmp(previous, payload, length) == 0
                   ? DOWNLOAD_RESULT_OK : DOWNLOAD_RESULT_WRONG_BLOCK_SEQUENCE;
    }
    if (flash_area_write(area, s_download.received, payload, length) != 0)
    {
        flash_area_close(area);
        return DOWNLOAD_RESULT_PROGRAMMING_FAILURE;
    }
    flash_area_close(area);

    next_received = s_download.received + length;
    s_download.received = next_received;
    s_download.previous_block_length = length;
    s_download.state = DOWNLOAD_STATE_TRANSFERRING;
    s_download.next_block_sequence_counter =
        (uint8_t)(s_download.next_block_sequence_counter + 1U);
    return DOWNLOAD_RESULT_OK;
}

download_result_t Download_Exit(void)
{
    if (s_download.state != DOWNLOAD_STATE_TRANSFERRING)
    {
        return DOWNLOAD_RESULT_SEQUENCE_ERROR;
    }
    if (s_download.received != s_download.image_size)
    {
        return DOWNLOAD_RESULT_SEQUENCE_ERROR;
    }

    s_download.state = DOWNLOAD_STATE_COMPLETE;
    return DOWNLOAD_RESULT_OK;
}
