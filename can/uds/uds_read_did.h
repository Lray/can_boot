#ifndef UDS_READ_DID_H
#define UDS_READ_DID_H

#include <stdint.h>

#define UDS_READ_DID_RESPONSE_MAX_SIZE 98U

typedef enum
{
    UDS_READ_DID_RESULT_OK = 0,
    UDS_READ_DID_RESULT_OUT_OF_RANGE,
    UDS_READ_DID_RESULT_BUILD_FAILED,
} uds_read_did_result_t;

typedef struct
{
    uint8_t *data;
    uint16_t capacity;
    uint16_t length;
} uds_read_did_response_t;

/**
 * Build a complete positive ReadDataByIdentifier response for one DID.
 *
 * @param did A validated 16-bit data identifier from a 0x22 request.
 * @param response Caller-owned response buffer and its writable capacity.
 * @return OK on success, OUT_OF_RANGE for an unsupported DID, or
 *         BUILD_FAILED when the response cannot be built.
 * @pre response->data has at least UDS_READ_DID_RESPONSE_MAX_SIZE bytes.
 */
uds_read_did_result_t UDS_ReadDid_Build(
    uint16_t did,
    uds_read_did_response_t *response);

#endif /* UDS_READ_DID_H */
