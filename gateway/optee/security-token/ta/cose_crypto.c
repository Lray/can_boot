#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tee_internal_api.h>

#include "t_cose_crypto.h"
#include "t_cose_standard_constants.h"

static TEE_ObjectHandle key_handle(struct t_cose_key key)
{
    return key.crypto_lib == T_COSE_CRYPTO_LIB_UNIDENTIFIED
               ? (TEE_ObjectHandle)key.k.key_ptr
               : TEE_HANDLE_NULL;
}

bool t_cose_crypto_is_algorithm_supported(int32_t algorithm)
{
    return algorithm == COSE_ALGORITHM_ES256 ||
           algorithm == COSE_ALGORITHM_SHA_256;
}

enum t_cose_err_t t_cose_crypto_sig_size(int32_t algorithm,
                                         struct t_cose_key key,
                                         size_t *signature_size)
{
    if (algorithm != COSE_ALGORITHM_ES256)
    {
        return T_COSE_ERR_UNSUPPORTED_SIGNING_ALG;
    }
    if (key_handle(key) == TEE_HANDLE_NULL)
    {
        return T_COSE_ERR_EMPTY_KEY;
    }
    if (signature_size == NULL)
    {
        return T_COSE_ERR_INVALID_ARGUMENT;
    }
    *signature_size = T_COSE_EC_P256_SIG_SIZE;
    return T_COSE_SUCCESS;
}

enum t_cose_err_t t_cose_crypto_sign(int32_t algorithm,
                                     struct t_cose_key key,
                                     struct q_useful_buf_c hash,
                                     struct q_useful_buf signature_buffer,
                                     struct q_useful_buf_c *signature_out)
{
    TEE_OperationHandle operation = TEE_HANDLE_NULL;
    TEE_Result result;
    uint32_t signature_size = T_COSE_EC_P256_SIG_SIZE;

    if (algorithm != COSE_ALGORITHM_ES256)
    {
        return T_COSE_ERR_UNSUPPORTED_SIGNING_ALG;
    }
    if (key_handle(key) == TEE_HANDLE_NULL)
    {
        return T_COSE_ERR_EMPTY_KEY;
    }
    if (hash.ptr == NULL || hash.len != T_COSE_CRYPTO_SHA256_SIZE ||
        signature_buffer.ptr == NULL ||
        signature_buffer.len < T_COSE_EC_P256_SIG_SIZE ||
        signature_out == NULL)
    {
        return T_COSE_ERR_INVALID_ARGUMENT;
    }
    result = TEE_AllocateOperation(&operation, TEE_ALG_ECDSA_P256,
                                   TEE_MODE_SIGN, 256u);
    if (result == TEE_SUCCESS)
    {
        result = TEE_SetOperationKey(operation, key_handle(key));
    }
    if (result == TEE_SUCCESS)
    {
        result = TEE_AsymmetricSignDigest(
            operation, NULL, 0u, hash.ptr, hash.len,
            signature_buffer.ptr, &signature_size);
    }
    if (operation != TEE_HANDLE_NULL)
    {
        TEE_FreeOperation(operation);
    }
    if (result != TEE_SUCCESS || signature_size != T_COSE_EC_P256_SIG_SIZE)
    {
        return T_COSE_ERR_SIG_FAIL;
    }
    *signature_out = (struct q_useful_buf_c){signature_buffer.ptr,
                                             signature_size};
    return T_COSE_SUCCESS;
}

enum t_cose_err_t t_cose_crypto_hash_start(struct t_cose_crypto_hash *context,
                                           int32_t algorithm)
{
    TEE_OperationHandle operation = TEE_HANDLE_NULL;
    TEE_Result result;

    if (context == NULL || algorithm != COSE_ALGORITHM_SHA_256)
    {
        return T_COSE_ERR_UNSUPPORTED_HASH;
    }
    result = TEE_AllocateOperation(&operation, TEE_ALG_SHA256,
                                   TEE_MODE_DIGEST, 0u);
    context->context.ptr = operation;
    context->status = result;
    return result == TEE_SUCCESS ? T_COSE_SUCCESS
                                 : T_COSE_ERR_HASH_GENERAL_FAIL;
}

void t_cose_crypto_hash_update(struct t_cose_crypto_hash *context,
                               struct q_useful_buf_c data)
{
    if (context != NULL && context->status == TEE_SUCCESS && data.ptr != NULL)
    {
        TEE_DigestUpdate((TEE_OperationHandle)context->context.ptr,
                         data.ptr, data.len);
    }
}

enum t_cose_err_t t_cose_crypto_hash_finish(struct t_cose_crypto_hash *context,
                                            struct q_useful_buf output,
                                            struct q_useful_buf_c *hash_out)
{
    TEE_OperationHandle operation;
    TEE_Result result;
    uint32_t hash_size = T_COSE_CRYPTO_SHA256_SIZE;

    if (context == NULL || output.ptr == NULL ||
        output.len < T_COSE_CRYPTO_SHA256_SIZE || hash_out == NULL)
    {
        return T_COSE_ERR_HASH_BUFFER_SIZE;
    }
    operation = (TEE_OperationHandle)context->context.ptr;
    if (context->status != TEE_SUCCESS || operation == TEE_HANDLE_NULL)
    {
        return T_COSE_ERR_HASH_GENERAL_FAIL;
    }
    result = TEE_DigestDoFinal(operation, NULL, 0u, output.ptr, &hash_size);
    TEE_FreeOperation(operation);
    context->context.ptr = NULL;
    context->status = result;
    if (result != TEE_SUCCESS || hash_size != T_COSE_CRYPTO_SHA256_SIZE)
    {
        return T_COSE_ERR_HASH_GENERAL_FAIL;
    }
    *hash_out = (struct q_useful_buf_c){output.ptr, hash_size};
    return T_COSE_SUCCESS;
}
