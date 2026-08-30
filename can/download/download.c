#include "download.h"

#include "boot_observation.h"
#include "erase_job.h"
#include "flash_map.h"
#include "shared/crc32.h"
#include "sysflash.h"

#include <stddef.h>
#include <string.h>

#define DOWNLOAD_CHECKPOINT_INTERVAL FLASH_PAGE_SIZE_BYTES
#define DOWNLOAD_JOURNAL_PAGE_COUNT 2U
#define DOWNLOAD_JOURNAL_RECORD_MAGIC 0x4A4E4C31U
#define DOWNLOAD_JOURNAL_RECORD_VERSION 7U
#define DOWNLOAD_JOURNAL_RECORD_SIZE_BYTES 64U
#define DOWNLOAD_JOURNAL_RECORDS_PER_PAGE \
    (FLASH_PAGE_SIZE_BYTES / DOWNLOAD_JOURNAL_RECORD_SIZE_BYTES)

#if ((DOWNLOAD_JOURNAL_PAGE_COUNT * FLASH_PAGE_SIZE_BYTES) != \
     FLASH_AREA_DOWNLOAD_JOURNAL_SIZE)
#error "Download journal must cover the mapped journal area."
#endif

typedef struct
{
    uint32_t magic;
    uint8_t version;
    uint8_t target_slot;
    uint16_t seq;
    uint32_t committed_offset;
    uint32_t image_size;
    uint8_t payload_id[PAYLOAD_ID_SIZE];
    uint32_t reserved[3];
    uint32_t self_crc32;
} download_journal_record_t;

typedef struct
{
    bool found;
    uint8_t page;
    uint16_t index;
    download_journal_record_t record;
} download_journal_scan_t;

typedef enum
{
    DOWNLOAD_JOURNAL_OK = 0,
    DOWNLOAD_JOURNAL_NOT_FOUND,
    DOWNLOAD_JOURNAL_READ_FAILED,
    DOWNLOAD_JOURNAL_ERASE_FAILED,
    DOWNLOAD_JOURNAL_PROGRAM_FAILED,
} download_journal_result_t;

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
    uint16_t last_block_length;
    uint32_t received;
    uint32_t image_size;
    uint8_t payload_id[PAYLOAD_ID_SIZE];
} download_session_t;

static download_session_t s_download;

static bool Download_JournalRecordErased(const download_journal_record_t *record)
{
    return flash_area_buffer_erased((const uint8_t *)record, sizeof(*record));
}

static bool Download_JournalRecordValid(const download_journal_record_t *record)
{
    if ((record->magic != DOWNLOAD_JOURNAL_RECORD_MAGIC) ||
        (record->version != DOWNLOAD_JOURNAL_RECORD_VERSION) ||
        ((record->target_slot != SLOT_A) && (record->target_slot != SLOT_B)) ||
        (record->image_size == 0U) ||
        (record->committed_offset > record->image_size) ||
        ((record->committed_offset != record->image_size) &&
         ((record->committed_offset % DOWNLOAD_CHECKPOINT_INTERVAL) != 0U)))
    {
        return false;
    }

    return Crc32_Compute((const uint8_t *)record,
                         offsetof(download_journal_record_t, self_crc32)) ==
           record->self_crc32;
}

static bool Download_JournalReadRecord(const struct flash_area *area,
                                       uint8_t page,
                                       uint16_t index,
                                       download_journal_record_t *record)
{
    return flash_area_read(
               area,
               ((uint32_t)page * FLASH_PAGE_SIZE_BYTES) +
                   ((uint32_t)index * DOWNLOAD_JOURNAL_RECORD_SIZE_BYTES),
               record,
               sizeof(*record)) == 0;
}

static download_journal_result_t Download_JournalFindFirstFree(
    const struct flash_area *area,
    uint8_t page,
    uint16_t *index)
{
    for (uint16_t current = 0U;
         current < DOWNLOAD_JOURNAL_RECORDS_PER_PAGE;
         current++)
    {
        download_journal_record_t record = {0};

        if (!Download_JournalReadRecord(area, page, current, &record))
        {
            return DOWNLOAD_JOURNAL_READ_FAILED;
        }
        if (Download_JournalRecordErased(&record))
        {
            *index = current;
            return DOWNLOAD_JOURNAL_OK;
        }
    }
    return DOWNLOAD_JOURNAL_NOT_FOUND;
}

static download_journal_result_t Download_JournalFindLatest(
    const struct flash_area *area,
    download_journal_scan_t *latest)
{
    *latest = (download_journal_scan_t){0};

    for (uint8_t page = 0U; page < DOWNLOAD_JOURNAL_PAGE_COUNT; page++)
    {
        for (uint16_t index = 0U;
             index < DOWNLOAD_JOURNAL_RECORDS_PER_PAGE;
             index++)
        {
            download_journal_record_t record = {0};

            if (!Download_JournalReadRecord(area, page, index, &record))
            {
                return DOWNLOAD_JOURNAL_READ_FAILED;
            }
            if (Download_JournalRecordErased(&record) ||
                !Download_JournalRecordValid(&record))
            {
                continue;
            }
            if (!latest->found ||
                ((record.seq != latest->record.seq) &&
                 ((uint16_t)(record.seq - latest->record.seq) < 0x8000U)))
            {
                latest->found = true;
                latest->page = page;
                latest->index = index;
                latest->record = record;
            }
        }
    }
    return DOWNLOAD_JOURNAL_OK;
}

static download_journal_result_t Download_JournalLoadLatest(
    download_journal_record_t *record)
{
    const struct flash_area *area = NULL;
    download_journal_scan_t latest = {0};
    download_journal_result_t result = DOWNLOAD_JOURNAL_READ_FAILED;

    if (flash_area_open(FLASH_AREA_DOWNLOAD_JOURNAL, &area) != 0)
    {
        return DOWNLOAD_JOURNAL_READ_FAILED;
    }

    result = Download_JournalFindLatest(area, &latest);
    flash_area_close(area);
    if (result != DOWNLOAD_JOURNAL_OK)
    {
        return result;
    }
    if (!latest.found)
    {
        return DOWNLOAD_JOURNAL_NOT_FOUND;
    }

    *record = latest.record;
    return DOWNLOAD_JOURNAL_OK;
}

static download_journal_result_t Download_JournalAppend(uint32_t committed_offset)
{
    const struct flash_area *area = NULL;
    download_journal_scan_t latest = {0};
    download_journal_record_t record = {0};
    download_journal_record_t verify = {0};
    download_journal_result_t result = DOWNLOAD_JOURNAL_PROGRAM_FAILED;
    uint16_t seq = 1U;
    uint8_t page = 0U;
    uint16_t index = 0U;
    uint8_t old_page = 0U;
    bool rollover = false;
    bool needs_erase = false;

    if (flash_area_open(FLASH_AREA_DOWNLOAD_JOURNAL, &area) != 0)
    {
        return DOWNLOAD_JOURNAL_PROGRAM_FAILED;
    }

    result = Download_JournalFindLatest(area, &latest);
    if (result != DOWNLOAD_JOURNAL_OK)
    {
        flash_area_close(area);
        return result;
    }

    if (latest.found)
    {
        page = latest.page;
        seq = (uint16_t)(latest.record.seq + 1U);
        result = Download_JournalFindFirstFree(area, page, &index);
        if (result == DOWNLOAD_JOURNAL_NOT_FOUND)
        {
            old_page = page;
            page ^= 1U;
            rollover = true;
            result = Download_JournalFindFirstFree(area, page, &index);
            if (result == DOWNLOAD_JOURNAL_NOT_FOUND)
            {
                index = 0U;
                needs_erase = true;
            }
        }
    }
    else
    {
        result = Download_JournalFindFirstFree(area, page, &index);
        if (result == DOWNLOAD_JOURNAL_NOT_FOUND)
        {
            page = 1U;
            result = Download_JournalFindFirstFree(area, page, &index);
            if (result == DOWNLOAD_JOURNAL_NOT_FOUND)
            {
                page = 0U;
                index = 0U;
                needs_erase = true;
            }
        }
    }

    if ((result != DOWNLOAD_JOURNAL_OK) && !needs_erase)
    {
        flash_area_close(area);
        return result;
    }

    if (needs_erase &&
        (flash_area_erase(area,
                          (uint32_t)page * FLASH_PAGE_SIZE_BYTES,
                          FLASH_PAGE_SIZE_BYTES) != 0))
    {
        flash_area_close(area);
        return DOWNLOAD_JOURNAL_ERASE_FAILED;
    }

    record.magic = DOWNLOAD_JOURNAL_RECORD_MAGIC;
    record.version = DOWNLOAD_JOURNAL_RECORD_VERSION;
    record.target_slot = s_download.target_slot;
    record.seq = seq;
    record.committed_offset = committed_offset;
    record.image_size = s_download.image_size;
    (void)memcpy(record.payload_id, s_download.payload_id, PAYLOAD_ID_SIZE);
    record.self_crc32 = Crc32_Compute(
        (const uint8_t *)&record,
        offsetof(download_journal_record_t, self_crc32));

    if ((flash_area_write(
             area,
             ((uint32_t)page * FLASH_PAGE_SIZE_BYTES) +
                 ((uint32_t)index * DOWNLOAD_JOURNAL_RECORD_SIZE_BYTES),
             &record,
             sizeof(record)) != 0) ||
        !Download_JournalReadRecord(area, page, index, &verify) ||
        !Download_JournalRecordValid(&verify) ||
        (memcmp(&verify, &record, sizeof(record)) != 0))
    {
        flash_area_close(area);
        return DOWNLOAD_JOURNAL_PROGRAM_FAILED;
    }

    if (rollover)
    {
        (void)flash_area_erase(area,
                               (uint32_t)old_page * FLASH_PAGE_SIZE_BYTES,
                               FLASH_PAGE_SIZE_BYTES);
    }
    flash_area_close(area);
    return DOWNLOAD_JOURNAL_OK;
}

static bool Download_CanResume(const download_journal_record_t *record)
{
    uint8_t image_prefix[FLASH_PROGRAM_UNIT] = {0};
    const struct flash_area *area = NULL;
    int area_id = flash_area_id_from_multi_image_slot(
        0,
        (int)record->target_slot);

    if ((record->target_slot != BootObservation_GetInactiveSlot()) ||
        (area_id < 0) || (flash_area_open((uint8_t)area_id, &area) != 0))
    {
        return false;
    }

    if (record->image_size > flash_area_get_size(area))
    {
        flash_area_close(area);
        return false;
    }
    if (record->committed_offset == 0U)
    {
        flash_area_close(area);
        return true;
    }
    if (flash_area_read(area, 0U, image_prefix, sizeof(image_prefix)) != 0)
    {
        flash_area_close(area);
        return false;
    }
    flash_area_close(area);

    return !flash_area_buffer_erased(image_prefix, sizeof(image_prefix));
}

static void Download_Reset(void)
{
    (void)memset(&s_download, 0, sizeof(s_download));
    s_download.state = DOWNLOAD_STATE_IDLE;
    s_download.target_slot = SLOT_INVALID;
    s_download.next_block_sequence_counter = 0x01U;
}

void Download_Init(void)
{
    EraseJob_Reset();
    Download_Reset();
}

download_result_t Download_Prepare(
    const uint8_t payload_id[PAYLOAD_ID_SIZE],
    uint32_t image_size)
{
    const struct flash_area *target_area = NULL;
    download_journal_record_t latest = {0};
    download_journal_result_t journal_result = DOWNLOAD_JOURNAL_NOT_FOUND;
    int target_area_id = -1;

    if ((payload_id == NULL) || (image_size == 0U))
    {
        return DOWNLOAD_RESULT_OUT_OF_RANGE;
    }
    if ((s_download.state == DOWNLOAD_STATE_PREPARING) ||
        (s_download.state == DOWNLOAD_STATE_READY) ||
        (s_download.state == DOWNLOAD_STATE_TRANSFERRING) ||
        (s_download.state == DOWNLOAD_STATE_COMPLETE))
    {
        return ((s_download.image_size == image_size) &&
                (memcmp(s_download.payload_id,
                        payload_id,
                        PAYLOAD_ID_SIZE) == 0)) ?
            DOWNLOAD_RESULT_OK : DOWNLOAD_RESULT_SEQUENCE_ERROR;
    }

    Download_Reset();
    s_download.target_slot = BootObservation_GetInactiveSlot();
    if (s_download.target_slot == SLOT_INVALID)
    {
        return DOWNLOAD_RESULT_REJECTED;
    }

    target_area_id = flash_area_id_from_multi_image_slot(
        0,
        (int)s_download.target_slot);
    if ((target_area_id < 0) ||
        (flash_area_open((uint8_t)target_area_id, &target_area) != 0))
    {
        Download_Reset();
        return DOWNLOAD_RESULT_OUT_OF_RANGE;
    }
    if (image_size > flash_area_get_size(target_area))
    {
        flash_area_close(target_area);
        Download_Reset();
        return DOWNLOAD_RESULT_OUT_OF_RANGE;
    }
    flash_area_close(target_area);

    s_download.image_size = image_size;
    (void)memcpy(s_download.payload_id, payload_id, PAYLOAD_ID_SIZE);

    journal_result = Download_JournalLoadLatest(&latest);
    if (journal_result == DOWNLOAD_JOURNAL_READ_FAILED)
    {
        Download_Reset();
        return DOWNLOAD_RESULT_PROGRAMMING_FAILURE;
    }
    if ((journal_result == DOWNLOAD_JOURNAL_OK) &&
        (latest.image_size == image_size) &&
        (memcmp(latest.payload_id, payload_id, PAYLOAD_ID_SIZE) == 0) &&
        Download_CanResume(&latest))
    {
        s_download.received = latest.committed_offset;
    }

    if (s_download.received == s_download.image_size)
    {
        s_download.state = DOWNLOAD_STATE_COMPLETE;
        return DOWNLOAD_RESULT_OK;
    }

    if (EraseJob_Start(s_download.target_slot, s_download.received) != 0)
    {
        Download_Reset();
        return DOWNLOAD_RESULT_PROGRAMMING_FAILURE;
    }

    s_download.state = DOWNLOAD_STATE_PREPARING;
    return DOWNLOAD_RESULT_OK;
}

static void Download_CompletePreparation(void)
{
    if ((s_download.target_slot != BootObservation_GetInactiveSlot()) ||
        (Download_JournalAppend(s_download.received) != DOWNLOAD_JOURNAL_OK))
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

download_result_t Download_Begin(
    const uint8_t payload_id[PAYLOAD_ID_SIZE],
    uint32_t image_size,
    uint8_t *target_slot_out,
    uint32_t *resume_offset_out)
{
    if ((payload_id == NULL) || (image_size == 0U) ||
        (target_slot_out == NULL) || (resume_offset_out == NULL))
    {
        return DOWNLOAD_RESULT_OUT_OF_RANGE;
    }
    if (s_download.state == DOWNLOAD_STATE_PREPARATION_FAILED)
    {
        return DOWNLOAD_RESULT_PROGRAMMING_FAILURE;
    }
    if ((s_download.state != DOWNLOAD_STATE_READY) &&
        (s_download.state != DOWNLOAD_STATE_COMPLETE))
    {
        return DOWNLOAD_RESULT_NOT_READY;
    }
    if ((s_download.image_size != image_size) ||
        (memcmp(s_download.payload_id, payload_id, PAYLOAD_ID_SIZE) != 0))
    {
        return DOWNLOAD_RESULT_SEQUENCE_ERROR;
    }

    *target_slot_out = s_download.target_slot;
    *resume_offset_out = s_download.received;
    if (s_download.state == DOWNLOAD_STATE_READY)
    {
        s_download.state = DOWNLOAD_STATE_TRANSFERRING;
    }
    return DOWNLOAD_RESULT_OK;
}

static download_result_t Download_CheckRepeatedBlock(const uint8_t *payload,
                                                      uint16_t length)
{
    const struct flash_area *area = NULL;
    uint8_t previous_block[DOWNLOAD_MAX_TRANSFER_PAYLOAD] = {0};
    int area_id = flash_area_id_from_multi_image_slot(
        0,
        (int)s_download.target_slot);

    if ((area_id < 0) || (flash_area_open((uint8_t)area_id, &area) != 0))
    {
        return DOWNLOAD_RESULT_PROGRAMMING_FAILURE;
    }
    if (flash_area_read(area,
                        s_download.received - length,
                        previous_block,
                        length) != 0)
    {
        flash_area_close(area);
        return DOWNLOAD_RESULT_PROGRAMMING_FAILURE;
    }
    flash_area_close(area);
    return (memcmp(previous_block, payload, length) == 0) ?
        DOWNLOAD_RESULT_OK : DOWNLOAD_RESULT_WRONG_BLOCK_SEQUENCE;
}

download_result_t Download_Transfer(uint8_t block_sequence_counter,
                                    const uint8_t *payload,
                                    uint16_t length)
{
    const struct flash_area *area = NULL;
    uint32_t remaining = 0U;
    uint32_t previous_received = 0U;
    uint32_t next_received = 0U;
    uint32_t previous_checkpoint = 0U;
    uint32_t current_checkpoint = 0U;
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
    if (block_sequence_counter != s_download.next_block_sequence_counter)
    {
        if ((block_sequence_counter ==
             (uint8_t)(s_download.next_block_sequence_counter - 1U)) &&
            (length == s_download.last_block_length))
        {
            return Download_CheckRepeatedBlock(payload, length);
        }
        return DOWNLOAD_RESULT_WRONG_BLOCK_SEQUENCE;
    }

    remaining = s_download.image_size - s_download.received;
    if (length > remaining)
    {
        return DOWNLOAD_RESULT_OUT_OF_RANGE;
    }

    area_id = flash_area_id_from_multi_image_slot(0, (int)s_download.target_slot);
    if ((area_id < 0) || (flash_area_open((uint8_t)area_id, &area) != 0))
    {
        return DOWNLOAD_RESULT_OUT_OF_RANGE;
    }
    if (flash_area_write(area, s_download.received, payload, length) != 0)
    {
        flash_area_close(area);
        return DOWNLOAD_RESULT_PROGRAMMING_FAILURE;
    }
    flash_area_close(area);

    previous_received = s_download.received;
    next_received = previous_received + length;
    previous_checkpoint = previous_received / DOWNLOAD_CHECKPOINT_INTERVAL;
    current_checkpoint = next_received / DOWNLOAD_CHECKPOINT_INTERVAL;
    if ((next_received != s_download.image_size) &&
        (current_checkpoint > previous_checkpoint) &&
        (Download_JournalAppend(
             current_checkpoint * DOWNLOAD_CHECKPOINT_INTERVAL) !=
         DOWNLOAD_JOURNAL_OK))
    {
        Download_Reset();
        return DOWNLOAD_RESULT_PROGRAMMING_FAILURE;
    }

    s_download.received = next_received;
    s_download.state = DOWNLOAD_STATE_TRANSFERRING;
    s_download.last_block_length = length;
    s_download.next_block_sequence_counter =
        (uint8_t)(s_download.next_block_sequence_counter + 1U);
    return DOWNLOAD_RESULT_OK;
}

download_result_t Download_Exit(void)
{
    if (s_download.state == DOWNLOAD_STATE_COMPLETE)
    {
        return DOWNLOAD_RESULT_OK;
    }
    if ((s_download.state != DOWNLOAD_STATE_TRANSFERRING) ||
        (s_download.received != s_download.image_size))
    {
        return DOWNLOAD_RESULT_SEQUENCE_ERROR;
    }
    if (Download_JournalAppend(s_download.image_size) != DOWNLOAD_JOURNAL_OK)
    {
        Download_Reset();
        return DOWNLOAD_RESULT_PROGRAMMING_FAILURE;
    }

    s_download.state = DOWNLOAD_STATE_COMPLETE;
    return DOWNLOAD_RESULT_OK;
}
