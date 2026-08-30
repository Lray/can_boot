#include "token_signer_tee.h"

#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>
#include <tee_client_api.h>

#include "ecdsa_sign_ta.h"
#include "t_cose_crypto.h"

struct TokenSignerTee
{
    TEEC_Context context;
    TEEC_Session session;
    int connected;
};

int token_signer_tee_connect(TokenSignerTee_t **tee_out)
{
    TokenSignerTee_t *tee = NULL;
    TEEC_UUID ta_uuid = ECDSA_SIGN_TA_UUID;

    if (tee_out == NULL)
    {
        return -1;
    }
    *tee_out = NULL;
    tee = (TokenSignerTee_t *)calloc(1, sizeof(*tee));
    if (tee == NULL)
    {
        return -1;
    }
    if (TEEC_InitializeContext(NULL, &tee->context) != TEEC_SUCCESS)
    {
        goto fail;
    }
    if (TEEC_OpenSession(&tee->context, &tee->session, &ta_uuid,
                         TEEC_LOGIN_PUBLIC, NULL, NULL, NULL) != TEEC_SUCCESS)
    {
        TEEC_FinalizeContext(&tee->context);
        goto fail;
    }
    tee->connected = 1;
    *tee_out = tee;
    return 0;

fail:
    free(tee);
    return -1;
}

void token_signer_tee_disconnect(TokenSignerTee_t *tee)
{
    if (tee == NULL)
    {
        return;
    }
    if (tee->connected)
    {
        TEEC_CloseSession(&tee->session);
        TEEC_FinalizeContext(&tee->context);
    }
    free(tee);
}

int token_signer_tee_get_public_key(TokenSignerTee_t *tee,
                                    uint8_t public_key[64])
{
    TEEC_Operation operation = {0};
    TEEC_Result rc;

    if (tee == NULL || public_key == NULL)
    {
        return -1;
    }
    operation.paramTypes =
        TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE, TEEC_NONE,
                         TEEC_NONE);
    operation.params[0].tmpref.buffer = public_key;
    operation.params[0].tmpref.size = 64u;
    rc = TEEC_InvokeCommand(&tee->session, ECDSA_SIGN_CMD_GET_PUBLIC_KEY,
                            &operation, NULL);
    return rc == TEEC_SUCCESS && operation.params[0].tmpref.size == 64u ? 0
                                                                        : -1;
}

int token_signer_tee_sign_digest(TokenSignerTee_t *tee,
                                 const uint8_t digest[32],
                                 uint8_t signature[64])
{
    TEEC_Operation operation = {0};
    TEEC_Result rc;

    if (tee == NULL || digest == NULL || signature == NULL)
    {
        return -1;
    }
    operation.paramTypes =
        TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_MEMREF_TEMP_INOUT,
                         TEEC_NONE, TEEC_NONE);
    operation.params[0].tmpref.buffer = (void *)digest;
    operation.params[0].tmpref.size = 32u;
    operation.params[1].tmpref.buffer = signature;
    operation.params[1].tmpref.size = 64u;
    rc = TEEC_InvokeCommand(&tee->session, ECDSA_SIGN_CMD_SIGN_DIGEST,
                            &operation, NULL);
    return rc == TEEC_SUCCESS && operation.params[1].tmpref.size == 64u ? 0
                                                                        : -1;
}

static TokenSignerTee_t *tee_key(struct t_cose_key key)
{
    return key.crypto_lib == T_COSE_CRYPTO_LIB_UNIDENTIFIED &&
                   key.k.key_ptr != NULL
               ? (TokenSignerTee_t *)key.k.key_ptr
               : NULL;
}

bool t_cose_crypto_is_algorithm_supported(int32_t cose_algorithm_id)
{
    return cose_algorithm_id == T_COSE_ALGORITHM_ES256;
}

enum t_cose_err_t t_cose_crypto_sig_size(int32_t algorithm,
                                         struct t_cose_key key,
                                         size_t *sig_size)
{
    if (sig_size == NULL || algorithm != T_COSE_ALGORITHM_ES256)
    {
        return algorithm == T_COSE_ALGORITHM_ES256 ? T_COSE_ERR_INVALID_ARGUMENT
                                                   : T_COSE_ERR_UNSUPPORTED_SIGNING_ALG;
    }
    if (tee_key(key) == NULL)
    {
        return T_COSE_ERR_EMPTY_KEY;
    }
    *sig_size = T_COSE_EC_P256_SIG_SIZE;
    return T_COSE_SUCCESS;
}

enum t_cose_err_t t_cose_crypto_sign(int32_t algorithm, struct t_cose_key key,
                                     struct q_useful_buf_c hash,
                                     struct q_useful_buf signature_buffer,
                                     struct q_useful_buf_c *signature_out)
{
    TokenSignerTee_t *tee;
    int rc;

    if (signature_out == NULL || hash.ptr == NULL || hash.len != 32u ||
        signature_buffer.ptr == NULL ||
        signature_buffer.len < T_COSE_EC_P256_SIG_SIZE)
    {
        return T_COSE_ERR_INVALID_ARGUMENT;
    }
    if (algorithm != T_COSE_ALGORITHM_ES256)
    {
        return T_COSE_ERR_UNSUPPORTED_SIGNING_ALG;
    }
    tee = tee_key(key);
    if (tee == NULL)
    {
        return T_COSE_ERR_EMPTY_KEY;
    }
    rc = token_signer_tee_sign_digest(tee, (const uint8_t *)hash.ptr,
                                      (uint8_t *)signature_buffer.ptr);
    if (rc != 0)
    {
        return T_COSE_ERR_SIG_FAIL;
    }
    *signature_out = (struct q_useful_buf_c){signature_buffer.ptr,
                                             T_COSE_EC_P256_SIG_SIZE};
    return T_COSE_SUCCESS;
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
    if (hash_ctx == NULL || hash_ctx->update_error != 0 || data.ptr == NULL)
    {
        return;
    }
    if (EVP_DigestUpdate(hash_ctx->evp_ctx, data.ptr, data.len) != 1)
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
