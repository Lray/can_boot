#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <stdint.h>

#include "shared/payload_id_format.h"

#define DOWNLOAD_MAX_TRANSFER_PAYLOAD 256U
#define DOWNLOAD_MAX_BLOCK_LENGTH 258U

typedef enum
{
    DOWNLOAD_RESULT_OK = 0,
    DOWNLOAD_RESULT_INCORRECT_LENGTH,
    DOWNLOAD_RESULT_OUT_OF_RANGE,
    DOWNLOAD_RESULT_SEQUENCE_ERROR,
    DOWNLOAD_RESULT_WRONG_BLOCK_SEQUENCE,
    DOWNLOAD_RESULT_REJECTED,
    DOWNLOAD_RESULT_PROGRAMMING_FAILURE,
    DOWNLOAD_RESULT_NOT_READY,
} download_result_t;

typedef enum
{
    DOWNLOAD_PREPARATION_IDLE = 0,
    DOWNLOAD_PREPARATION_PENDING,
    DOWNLOAD_PREPARATION_READY,
    DOWNLOAD_PREPARATION_FAILED,
} download_preparation_status_t;

/** Clears the RAM-resident download session. */
void Download_Init(void);

/**
 * Prepare a resumable image transfer and start erasing its target suffix.
 *
 * The request identity is the SHA-256 of the complete MCUboot image byte
 * stream, supplied by the unlocked UDS programming session.
 *
 * @param payload_id Exact image byte-stream SHA-256; must be non-NULL.
 * @param image_size Exact payload length; must fit in the inactive slot.
 */
download_result_t Download_Prepare(
    const uint8_t payload_id[PAYLOAD_ID_SIZE],
    uint32_t image_size);

/** Advance the active Flash erase job by one sector. */
void Download_Poll(void);

/** Report the current preparation state for RoutineControl results. */
download_preparation_status_t Download_GetPreparationStatus(void);

/**
 * Validate a prepared image identity and open the data-transfer phase.
 */
download_result_t Download_Begin(
    const uint8_t payload_id[PAYLOAD_ID_SIZE],
    uint32_t image_size,
    uint8_t *target_slot_out,
    uint32_t *resume_offset_out);

/** Program one validated TransferData block into the inactive slot. */
download_result_t Download_Transfer(uint8_t block_sequence_counter,
                                    const uint8_t *payload,
                                    uint16_t length);

/** Finish a complete transfer and persist its terminal transfer checkpoint. */
download_result_t Download_Exit(void);

#endif /* DOWNLOAD_H */
