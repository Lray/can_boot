#ifndef UDS_TRANSACTION_H
#define UDS_TRANSACTION_H

#include <stddef.h>
#include <stdint.h>

#include "uds_client.h"

/** Execute one UDS request/response transaction, handling NRC 0x78 ResponsePending. */
int uds_transaction_request(UdsClient *client, const uint8_t *request, size_t request_len,
                            uint8_t *response, size_t response_cap, size_t *response_len);

#endif