#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <openssl/core_names.h>
#include <openssl/evp.h>

#include "security_token.h"
#include "token_builder.h"

void security_contract_set_public_key(const uint8_t public_key[64]);

static EVP_PKEY *make_signing_key(uint8_t public_key[64])
{
    EVP_PKEY *key;
    uint8_t encoded[65];
    size_t encoded_len = 0u;

    key = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "prime256v1");
    assert(key != NULL);
    assert(EVP_PKEY_get_octet_string_param(key, OSSL_PKEY_PARAM_PUB_KEY,
                                           encoded, sizeof(encoded),
                                           &encoded_len) == 1);
    assert(encoded_len == sizeof(encoded));
    assert(encoded[0] == 0x04u);
    memcpy(public_key, encoded + 1u, 64u);
    return key;
}

int main(void)
{
    static const uint8_t seed[SECURITY_ACCESS_SEED_SIZE] = {
        0xA1u, 0xB2u, 0xC3u, 0xD4u};
    static const uint8_t wrong_seed[SECURITY_ACCESS_SEED_SIZE] = {
        0xA1u, 0xB2u, 0xC3u, 0xD5u};
    uint8_t public_key[64];
    uint8_t token[SECURITY_TOKEN_MAX_SIZE];
    uint8_t tampered[SECURITY_TOKEN_MAX_SIZE];
    size_t token_len = 0u;
    EVP_PKEY *key = make_signing_key(public_key);
    struct t_cose_key signing_key = T_COSE_NULL_KEY;

    security_contract_set_public_key(public_key);
    signing_key.crypto_lib = T_COSE_CRYPTO_LIB_OPENSSL;
    signing_key.k.key_ptr = key;
    assert(token_builder_sign_seed(signing_key, seed, sizeof(seed), token,
                                   sizeof(token), &token_len) ==
           TOKEN_BUILDER_OK);
    assert(token_len > 64u && token_len <= sizeof(token));
    assert(SecurityToken_VerifyOtaEntry(seed, token, token_len) ==
           SECURITY_TOKEN_RESULT_OK);
    assert(SecurityToken_VerifyOtaEntry(wrong_seed, token, token_len) ==
           SECURITY_TOKEN_RESULT_WRONG_SEED);

    memcpy(tampered, token, token_len);
    tampered[token_len - 1u] ^= 0x01u;
    assert(SecurityToken_VerifyOtaEntry(seed, tampered, token_len) ==
           SECURITY_TOKEN_RESULT_BAD_SIGNATURE);
    assert(token_builder_sign_seed(signing_key, seed, sizeof(seed), token, 8u,
                                   &token_len) == TOKEN_BUILDER_TOO_LARGE);

    EVP_PKEY_free(key);
    return 0;
}
