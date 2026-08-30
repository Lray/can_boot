#ifndef SECURITY_TOKEN_H
#define SECURITY_TOKEN_H

#include <stddef.h>
#include <stdint.h>

#include "security_token_profile.h"

/**
 * Verify that token authorizes entry to an OTA programming session.
 *
 * Package identity, image integrity and rollback policy are deliberately not
 * token claims.  They remain owned by the UDS/download lifecycle and MCUboot.
 */
security_token_result_t SecurityToken_VerifyOtaEntry(
    const uint8_t seed[SECURITY_ACCESS_SEED_SIZE],
    const uint8_t *token,
    size_t token_length);

#endif /* SECURITY_TOKEN_H */
