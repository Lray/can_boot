#ifndef RESUME_TRANSFER_H
#define RESUME_TRANSFER_H

#include <stdint.h>

#include "uds_client.h"

#define RESUME_TRANSFER_ERR_INVALID_ARG (-401)
#define RESUME_TRANSFER_ERR_IDENTITY_MISMATCH (-402)
#define RESUME_TRANSFER_ERR_BLOCK_PAYLOAD (-403)
#define RESUME_TRANSFER_ERR_CHECKPOINT_ALIGNMENT (-405)

/* Bind, validate, and execute the remaining image transfer. */
int resume_transfer_execute(UdsClient *client,
                            const uint8_t payload_id[PAYLOAD_ID_SIZE],
                            uint32_t image_size,
                            const uint8_t *image,
                            uint8_t *target_slot_out);

#endif /* RESUME_TRANSFER_H */
