#ifndef UDS_READ_DID_H
#define UDS_READ_DID_H

#include <stdint.h>

#define UDS_READ_DID_PAYLOAD_MAX_SIZE 16U

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
} uds_read_did_payload_t;

uds_read_did_result_t UDS_ReadDid_BuildPayload(
    uint16_t did,
    uds_read_did_payload_t *payload);

#endif
