#ifndef TOKEN_SIGNER_PROTOCOL_H
#define TOKEN_SIGNER_PROTOCOL_H

#include <stddef.h>
#include <string.h>

#define TOKEN_SIGNER_RESPONSE_STATUS_OK 0u
#define TOKEN_SIGNER_RESPONSE_STATUS_MALFORMED 1u
#define TOKEN_SIGNER_RESPONSE_STATUS_POLICY 2u
#define TOKEN_SIGNER_RESPONSE_STATUS_KEY 3u
#define TOKEN_SIGNER_RESPONSE_STATUS_SIGNING 4u
#define TOKEN_SIGNER_RESPONSE_STATUS_TOO_LARGE 5u
#define TOKEN_SIGNER_RESPONSE_STATUS_INTERNAL 6u
#define TOKEN_SIGNER_RESPONSE_STATUS_MAX 6u
#define TOKEN_SIGNER_MAX_REQUEST_SIZE 512u
#define TOKEN_SIGNER_MAX_RESPONSE_SIZE 2048u

/* Compare a received buffer against a fixed expected buffer. */
static inline int token_signer_buffer_equals(const void *actual, size_t actual_len,
                                             const void *expected, size_t expected_len)
{
    return actual != NULL && actual_len == expected_len &&
                   memcmp(actual, expected, expected_len) == 0
               ? 1
               : 0;
}

#endif