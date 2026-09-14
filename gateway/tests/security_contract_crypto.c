#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/params.h>

#include "security_crypto_port.h"
#include "t_cose_crypto.h"
#include "t_cose_standard_constants.h"

static uint8_t s_public_key[64];
static int s_public_key_ready;

void security_contract_set_public_key(const uint8_t public_key[64])
{
    memcpy(s_public_key, public_key, sizeof(s_public_key));
    s_public_key_ready = 1;
}

bool SecurityCrypto_GetProvisionedPublicKey(const uint8_t **key_out,
                                            size_t *key_length_out)
{
    if (key_out == NULL || key_length_out == NULL || !s_public_key_ready)
    {
        return false;
    }
    *key_out = s_public_key;
    *key_length_out = sizeof(s_public_key);
    return true;
}

bool t_cose_crypto_is_algorithm_supported(int32_t cose_algorithm_id)
{
    return cose_algorithm_id == COSE_ALGORITHM_ES256 ||
           cose_algorithm_id == COSE_ALGORITHM_SHA_256;
}

enum t_cose_err_t t_cose_crypto_sig_size(int32_t cose_algorithm_id,
                                         struct t_cose_key signing_key,
                                         size_t *sig_size)
{
    if (cose_algorithm_id != COSE_ALGORITHM_ES256 ||
        signing_key.k.key_ptr == NULL || sig_size == NULL)
    {
        return T_COSE_ERR_UNSUPPORTED_SIGNING_ALG;
    }
    *sig_size = 64u;
    return T_COSE_SUCCESS;
}

enum t_cose_err_t t_cose_crypto_sign(int32_t cose_algorithm_id,
                                     struct t_cose_key signing_key,
                                     struct q_useful_buf_c hash_to_sign,
                                     struct q_useful_buf signature_buffer,
                                     struct q_useful_buf_c *signature_out)
{
    EVP_PKEY_CTX *context = NULL;
    ECDSA_SIG *decoded = NULL;
    const BIGNUM *r = NULL;
    const BIGNUM *s = NULL;
    uint8_t der[80];
    const unsigned char *der_cursor = der;
    size_t der_len = sizeof(der);
    int ok = 0;

    if (cose_algorithm_id != COSE_ALGORITHM_ES256 ||
        signing_key.crypto_lib != T_COSE_CRYPTO_LIB_OPENSSL ||
        signing_key.k.key_ptr == NULL || hash_to_sign.ptr == NULL ||
        hash_to_sign.len != 32u || signature_buffer.ptr == NULL ||
        signature_buffer.len < 64u || signature_out == NULL)
    {
        return T_COSE_ERR_INVALID_ARGUMENT;
    }
    context = EVP_PKEY_CTX_new((EVP_PKEY *)signing_key.k.key_ptr, NULL);
    if (context != NULL && EVP_PKEY_sign_init(context) == 1 &&
        EVP_PKEY_CTX_set_signature_md(context, EVP_sha256()) == 1 &&
        EVP_PKEY_sign(context, der, &der_len, hash_to_sign.ptr,
                      hash_to_sign.len) == 1)
    {
        decoded = d2i_ECDSA_SIG(NULL, &der_cursor, (long)der_len);
    }
    if (decoded != NULL)
    {
        ECDSA_SIG_get0(decoded, &r, &s);
        ok = BN_bn2binpad(r, signature_buffer.ptr, 32) == 32 &&
             BN_bn2binpad(s, (uint8_t *)signature_buffer.ptr + 32u, 32) == 32;
    }
    ECDSA_SIG_free(decoded);
    EVP_PKEY_CTX_free(context);
    if (!ok)
    {
        return T_COSE_ERR_SIG_FAIL;
    }
    *signature_out = (struct q_useful_buf_c){signature_buffer.ptr, 64u};
    return T_COSE_SUCCESS;
}

enum t_cose_err_t t_cose_crypto_verify(int32_t cose_algorithm_id,
                                       struct t_cose_key verification_key,
                                       struct q_useful_buf_c kid,
                                       struct q_useful_buf_c hash_to_verify,
                                       struct q_useful_buf_c signature)
{
    EVP_PKEY_CTX *key_context = NULL;
    EVP_PKEY_CTX *verify_context = NULL;
    EVP_PKEY *key = NULL;
    ECDSA_SIG *decoded = NULL;
    BIGNUM *r = NULL;
    BIGNUM *s = NULL;
    uint8_t encoded_point[65];
    uint8_t der[80];
    unsigned char *der_cursor = der;
    int der_len = 0;
    int verify_rc = 0;

    (void)kid;
    if (cose_algorithm_id != COSE_ALGORITHM_ES256 ||
        verification_key.k.key_ptr == NULL || hash_to_verify.ptr == NULL ||
        hash_to_verify.len != 32u || signature.ptr == NULL ||
        signature.len != 64u)
    {
        return T_COSE_ERR_UNSUPPORTED_SIGNING_ALG;
    }
    encoded_point[0] = 0x04u;
    memcpy(encoded_point + 1u, verification_key.k.key_ptr, 64u);
    key_context = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    if (key_context != NULL && EVP_PKEY_fromdata_init(key_context) == 1)
    {
        char group_name[] = "prime256v1";
        OSSL_PARAM parameters[] = {
            OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME,
                                             group_name, 0u),
            OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY,
                                              encoded_point,
                                              sizeof(encoded_point)),
            OSSL_PARAM_construct_end()};

        if (EVP_PKEY_fromdata(key_context, &key, EVP_PKEY_PUBLIC_KEY,
                              parameters) == 1)
        {
            decoded = ECDSA_SIG_new();
            r = BN_bin2bn(signature.ptr, 32, NULL);
            s = BN_bin2bn((const uint8_t *)signature.ptr + 32u, 32, NULL);
        }
    }
    if (decoded != NULL && r != NULL && s != NULL &&
        ECDSA_SIG_set0(decoded, r, s) == 1)
    {
        r = NULL;
        s = NULL;
        der_len = i2d_ECDSA_SIG(decoded, &der_cursor);
    }
    verify_context = key != NULL ? EVP_PKEY_CTX_new(key, NULL) : NULL;
    if (der_len > 0 && verify_context != NULL &&
        EVP_PKEY_verify_init(verify_context) == 1 &&
        EVP_PKEY_CTX_set_signature_md(verify_context, EVP_sha256()) == 1)
    {
        verify_rc = EVP_PKEY_verify(verify_context, der, (size_t)der_len,
                                    hash_to_verify.ptr, hash_to_verify.len);
    }
    BN_free(r);
    BN_free(s);
    ECDSA_SIG_free(decoded);
    EVP_PKEY_CTX_free(verify_context);
    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(key_context);
    return verify_rc == 1 ? T_COSE_SUCCESS : T_COSE_ERR_SIG_VERIFY;
}

enum t_cose_err_t t_cose_crypto_hash_start(struct t_cose_crypto_hash *hash_ctx,
                                           int32_t hash_algorithm)
{
    if (hash_ctx == NULL || hash_algorithm != COSE_ALGORITHM_SHA_256)
    {
        return T_COSE_ERR_UNSUPPORTED_HASH;
    }
    hash_ctx->evp_ctx = EVP_MD_CTX_new();
    hash_ctx->update_error = hash_ctx->evp_ctx == NULL ||
                             EVP_DigestInit_ex(hash_ctx->evp_ctx, EVP_sha256(),
                                               NULL) != 1;
    hash_ctx->cose_hash_alg_id = hash_algorithm;
    return hash_ctx->update_error == 0 ? T_COSE_SUCCESS
                                       : T_COSE_ERR_HASH_GENERAL_FAIL;
}

void t_cose_crypto_hash_update(struct t_cose_crypto_hash *hash_ctx,
                               struct q_useful_buf_c data)
{
    if (hash_ctx != NULL && hash_ctx->update_error == 0 && data.ptr != NULL &&
        EVP_DigestUpdate(hash_ctx->evp_ctx, data.ptr, data.len) != 1)
    {
        hash_ctx->update_error = 1;
    }
}

enum t_cose_err_t t_cose_crypto_hash_finish(struct t_cose_crypto_hash *hash_ctx,
                                            struct q_useful_buf output,
                                            struct q_useful_buf_c *hash_out)
{
    unsigned int length = 0u;
    int failed;

    if (hash_ctx == NULL || hash_out == NULL || output.ptr == NULL ||
        output.len < 32u)
    {
        return T_COSE_ERR_HASH_BUFFER_SIZE;
    }
    failed = hash_ctx->update_error != 0 ||
             EVP_DigestFinal_ex(hash_ctx->evp_ctx, output.ptr, &length) != 1 ||
             length != 32u;
    EVP_MD_CTX_free(hash_ctx->evp_ctx);
    hash_ctx->evp_ctx = NULL;
    if (failed)
    {
        return T_COSE_ERR_HASH_GENERAL_FAIL;
    }
    *hash_out = (struct q_useful_buf_c){output.ptr, length};
    return T_COSE_SUCCESS;
}
