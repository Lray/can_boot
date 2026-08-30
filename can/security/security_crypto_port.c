#include "security_crypto_port.h"

#include "security_token_profile.h"

#include <string.h>

#include "t_cose_crypto.h"
#include "t_cose_standard_constants.h"
#include "tinycrypt/constants.h"
#include "tinycrypt/ecc.h"
#include "tinycrypt/ecc_dsa.h"
#include "tinycrypt/sha256.h"

/* CAN OTA trust anchor generated from the external private key. */
static const uint8_t s_can_ota_public_key[64] = {
    0x9AU, 0x57U, 0x4EU, 0x42U, 0x9BU, 0x51U, 0xD2U, 0x01U,
    0x21U, 0x71U, 0x16U, 0x37U, 0xD8U, 0x31U, 0xA4U, 0x95U,
    0xE8U, 0x1CU, 0x2AU, 0x8FU, 0x8DU, 0x49U, 0xC5U, 0x6AU,
    0x61U, 0x89U, 0x28U, 0xD5U, 0x84U, 0xA4U, 0xB9U, 0xD3U,
    0x22U, 0x32U, 0x5CU, 0x84U, 0x5EU, 0xD9U, 0xEEU, 0x46U,
    0x3FU, 0x25U, 0xEAU, 0xEAU, 0x00U, 0x8CU, 0x94U, 0x11U,
    0x65U, 0x9DU, 0xC3U, 0xB7U, 0x77U, 0x29U, 0xFBU, 0xB0U,
    0x3DU, 0x84U, 0x4CU, 0xD8U, 0xF2U, 0xF5U, 0x1AU, 0x80U,
};

bool SecurityCrypto_GetProvisionedPublicKey(
    const uint8_t **key_out,
    size_t *key_length_out)
{
    if ((key_out == 0) || (key_length_out == 0))
    {
        return false;
    }

    *key_out = s_can_ota_public_key;
    *key_length_out = 64U;
    return true;
}

bool t_cose_crypto_is_algorithm_supported(int32_t cose_algorithm_id)
{
    return ((cose_algorithm_id == COSE_ALGORITHM_ES256) ||
            (cose_algorithm_id == COSE_ALGORITHM_SHA_256));
}

enum t_cose_err_t t_cose_crypto_sig_size(int32_t cose_algorithm_id,
                                         struct t_cose_key signing_key,
                                         size_t *sig_size)
{
    (void)signing_key;

    if ((sig_size == 0) || (cose_algorithm_id != COSE_ALGORITHM_ES256))
    {
        return T_COSE_ERR_UNSUPPORTED_SIGNING_ALG;
    }

    *sig_size = SECURITY_TOKEN_ES256_SIGNATURE_SIZE;
    return T_COSE_SUCCESS;
}

enum t_cose_err_t t_cose_crypto_sign(int32_t cose_algorithm_id,
                                     struct t_cose_key signing_key,
                                     struct q_useful_buf_c hash_to_sign,
                                     struct q_useful_buf signature_buffer,
                                     struct q_useful_buf_c *signature)
{
    (void)cose_algorithm_id;
    (void)signing_key;
    (void)hash_to_sign;
    (void)signature_buffer;
    (void)signature;
    return T_COSE_ERR_UNSUPPORTED_SIGNING_ALG;
}

enum t_cose_err_t t_cose_crypto_verify(int32_t cose_algorithm_id,
                                       struct t_cose_key verification_key,
                                       struct q_useful_buf_c kid,
                                       struct q_useful_buf_c hash_to_verify,
                                       struct q_useful_buf_c signature)
{
    const uint8_t *public_key;

    (void)kid;
    if ((cose_algorithm_id != COSE_ALGORITHM_ES256) ||
        (hash_to_verify.ptr == 0) ||
        (hash_to_verify.len != SECURITY_TOKEN_SHA256_SIZE) ||
        (signature.ptr == 0) ||
        (signature.len != SECURITY_TOKEN_ES256_SIGNATURE_SIZE))
    {
        return T_COSE_ERR_UNSUPPORTED_SIGNING_ALG;
    }

    if (verification_key.k.key_ptr == 0)
    {
        return T_COSE_ERR_UNKNOWN_KEY;
    }

    public_key = (const uint8_t *)verification_key.k.key_ptr;
    if (uECC_verify(public_key,
                    (const uint8_t *)hash_to_verify.ptr,
                    (unsigned int)hash_to_verify.len,
                    (const uint8_t *)signature.ptr,
                    uECC_secp256r1()) != TC_CRYPTO_SUCCESS)
    {
        return T_COSE_ERR_SIG_VERIFY;
    }

    return T_COSE_SUCCESS;
}

enum t_cose_err_t t_cose_crypto_hash_start(struct t_cose_crypto_hash *hash_ctx,
                                           int32_t cose_hash_alg_id)
{
    if ((hash_ctx == 0) || (cose_hash_alg_id != COSE_ALGORITHM_SHA_256))
    {
        return T_COSE_ERR_UNSUPPORTED_HASH;
    }

    sha256_init(&hash_ctx->b_con_hash_context);
    return T_COSE_SUCCESS;
}

void t_cose_crypto_hash_update(struct t_cose_crypto_hash *hash_ctx,
                               struct q_useful_buf_c data_to_hash)
{
    if ((hash_ctx != 0) && (data_to_hash.ptr != 0) &&
        (data_to_hash.len != 0U))
    {
        sha256_update(&hash_ctx->b_con_hash_context,
                      (const BYTE *)data_to_hash.ptr,
                      data_to_hash.len);
    }
}

enum t_cose_err_t t_cose_crypto_hash_finish(
    struct t_cose_crypto_hash *hash_ctx,
    struct q_useful_buf buffer_to_hold_result,
    struct q_useful_buf_c *hash_result)
{
    if ((hash_ctx == 0) ||
        (hash_result == 0) ||
        (buffer_to_hold_result.ptr == 0) ||
        (buffer_to_hold_result.len < SECURITY_TOKEN_SHA256_SIZE))
    {
        return T_COSE_ERR_HASH_BUFFER_SIZE;
    }

    sha256_final(&hash_ctx->b_con_hash_context,
                 (BYTE *)buffer_to_hold_result.ptr);
    hash_result->ptr = buffer_to_hold_result.ptr;
    hash_result->len = SECURITY_TOKEN_SHA256_SIZE;
    return T_COSE_SUCCESS;
}
