#ifndef SECURITY_TOKEN_PROFILE_H
#define SECURITY_TOKEN_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#include "shared/security_access_level.h"
#include "shared/uds_protocol.h"

/* Shared token profile contract. See shared/security_token_profile.h. */
#include "../../shared/security_token_profile.h"

typedef enum
{
    SECURITY_TOKEN_RESULT_OK = 0,
    SECURITY_TOKEN_RESULT_BAD_FORMAT,
    SECURITY_TOKEN_RESULT_BAD_SIGNATURE,
    SECURITY_TOKEN_RESULT_WRONG_SEED,
    SECURITY_TOKEN_RESULT_UNSUPPORTED_KEY,
    SECURITY_TOKEN_RESULT_UNSUPPORTED_TOKEN,
    SECURITY_TOKEN_RESULT_REPLAY,
} security_token_result_t;

typedef struct
{
    bool signature_valid;
    uint64_t freshness_nonce;
    uint8_t seed_challenge[SECURITY_ACCESS_SEED_SIZE];
} security_token_claims_t;

#endif /* SECURITY_TOKEN_PROFILE_H */
