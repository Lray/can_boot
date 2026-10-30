#ifndef TOKEN_BUILDER_H
#define TOKEN_BUILDER_H

#include <stddef.h>
#include <stdint.h>

#include "t_cose/t_cose_common.h"

typedef enum
{
    TOKEN_BUILD_OK = 0,
    TOKEN_BUILD_INVALID_ARGUMENT,
    TOKEN_BUILD_TOO_LARGE,
    TOKEN_BUILD_SIGNING_FAILED
} TokenBuildResult_t;

TokenBuildResult_t token_build(
    struct t_cose_key signing_key, const uint8_t seed[4],
    uint8_t *token_out, size_t token_cap, size_t *token_len_out);

#endif
