#ifndef SECURITY_TOKEN_COSE_H
#define SECURITY_TOKEN_COSE_H

#include <stddef.h>
#include <stdint.h>

#include "security_token_profile.h"

/* COSE_Sign1 verification and CBOR claims decoding. */
security_token_result_t SecurityToken_CoseVerify(
    const uint8_t *token,
    size_t token_length,
    security_token_claims_t *claims);

#endif /* SECURITY_TOKEN_COSE_H */
