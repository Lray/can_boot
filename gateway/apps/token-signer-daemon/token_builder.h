#ifndef TOKEN_BUILDER_H
#define TOKEN_BUILDER_H

#include <stddef.h>
#include <stdint.h>

#include "t_cose/t_cose_common.h"

typedef enum
{
    TOKEN_BUILDER_OK = 0,
    TOKEN_BUILDER_INVALID_ARGUMENT,
    TOKEN_BUILDER_TOO_LARGE,
    TOKEN_BUILDER_SIGNING_FAILED
} TokenBuilderResult_t;

TokenBuilderResult_t token_builder_sign_seed(
    struct t_cose_key signing_key, const uint8_t *seed, size_t seed_len,
    uint8_t *token_out, size_t token_cap, size_t *token_len_out);

#endif
