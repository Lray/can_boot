#ifndef DOWNLOAD_H
#define DOWNLOAD_H

#include <stdint.h>


#define DOWNLOAD_MAX_TRANSFER_PAYLOAD 256U
#define DOWNLOAD_MAX_BLOCK_LENGTH 258U

typedef enum
{
    DOWNLOAD_RESULT_OK = 0,
    DOWNLOAD_RESULT_INCORRECT_LENGTH,
    DOWNLOAD_RESULT_OUT_OF_RANGE,
    DOWNLOAD_RESULT_TRANSFER_SUSPENDED,
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
/** Abort the current diagnostic download without erasing programmed data. */
void Download_Abort(void);

/**
 * Select the inactive slot and start erasing it.
 *
 * Erasing needs no request parameters.
 */
download_result_t Download_Prepare(void);

/** Advance the active Flash erase job by one sector. */
void Download_Poll(void);

/** Report the current preparation state for RoutineControl results. */
download_preparation_status_t Download_GetPreparationStatus(void);

/**
 * Validate the requested image against the prepared slot and open the
 * data-transfer phase.
 *
 * @param image_size Exact payload length; must fit in the inactive slot.
 */
download_result_t Download_Begin(uint32_t image_size);

/** Program one validated TransferData block into the inactive slot. */
download_result_t Download_Transfer(uint8_t block_sequence_counter,
                                    const uint8_t *payload,
                                    uint16_t length);

/**
 * Finish a complete transfer.
 *
 * @return OK when the whole image was received, or SEQUENCE_ERROR when no
 *         transfer is active (ISO 14229-1 0x37 -> NRC 0x24).
 */
download_result_t Download_Exit(void);

#endif /* DOWNLOAD_H */
