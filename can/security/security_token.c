#include "security_token.h"

#include "security_crypto_port.h"
#include "security_token_cose.h"

#include <string.h>

static bool SecurityToken_MemoryEqual(const uint8_t *actual,
                                      const uint8_t *expected,
                                      uint16_t length)
{
    uint8_t diff = 0U;

    for (uint16_t index = 0U; index < length; index++)
    {
        diff |= (uint8_t)(actual[index] ^ expected[index]);
    }

    return diff == 0U;
}

static uint64_t SecurityToken_ExpectedFreshnessNonce(
    const uint8_t seed[SECURITY_ACCESS_SEED_SIZE])
{
    uint32_t seed_value = ((uint32_t)seed[0] << 24) |
                          ((uint32_t)seed[1] << 16) |
                          ((uint32_t)seed[2] << 8) |
                          (uint32_t)seed[3];

    return ((uint64_t)seed_value << 32) | 1ULL;
}

static security_token_result_t SecurityToken_ValidateClaims(
    const uint8_t seed[SECURITY_ACCESS_SEED_SIZE],
    const security_token_claims_t *claims)
{
    if (claims == 0)
    {
        return SECURITY_TOKEN_RESULT_BAD_FORMAT;
    }

    if (!claims->signature_valid)
    {
        return SECURITY_TOKEN_RESULT_BAD_SIGNATURE;
    }

    if (!SecurityToken_MemoryEqual(seed,
                                   claims->seed_challenge,
                                   SECURITY_ACCESS_SEED_SIZE))
    {
        return SECURITY_TOKEN_RESULT_WRONG_SEED;
    }

    if (claims->freshness_nonce != SecurityToken_ExpectedFreshnessNonce(seed))
    {
        return SECURITY_TOKEN_RESULT_REPLAY;
    }

    return SECURITY_TOKEN_RESULT_OK;
}

security_token_result_t SecurityToken_VerifyOtaEntry(
    const uint8_t seed[SECURITY_ACCESS_SEED_SIZE],
    const uint8_t *token,
    size_t token_length)
{
    security_token_claims_t claims;
    security_token_result_t result;

    if ((seed == 0) || (token == 0) || (token_length == 0U) ||
        (token_length > SECURITY_TOKEN_MAX_SIZE))
    {
        return SECURITY_TOKEN_RESULT_BAD_FORMAT;
    }

    memset(&claims, 0, sizeof(claims));
    result = SecurityToken_CoseVerify(token, token_length, &claims);
    if (result != SECURITY_TOKEN_RESULT_OK)
    {
        return result;
    }

    result = SecurityToken_ValidateClaims(seed, &claims);
    if (result != SECURITY_TOKEN_RESULT_OK)
    {
        return result;
    }

    return SECURITY_TOKEN_RESULT_OK;
}
