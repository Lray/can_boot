#ifndef TRANSFER_H
#define TRANSFER_H

#include <stdint.h>

#include "uds_client.h"

#define TRANSFER_ERR_INVALID_ARG (-401)
#define TRANSFER_ERR_IDENTITY_MISMATCH (-402)
#define TRANSFER_ERR_BLOCK_PAYLOAD (-403)

/* Bind, validate, and execute the image transfer. */
int transfer_execute(UdsClient *client,
                     const uint8_t payload_id[PAYLOAD_ID_SIZE],
                     uint32_t image_size,
                     const uint8_t *image,
                     uint8_t *target_slot_out);

#endif /* TRANSFER_H */