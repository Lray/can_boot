/*
 * ecdsa_sign_ta.c
 *
 * ECDSA-P256 signing trusted application for the T527 OP-TEE secure world.
 *
 * The signing keypair (d || x || y, 96 bytes) is provisioned into the
 * keybox by DragonSN and is never exposed to the normal world: the TA
 * loads it inside the secure world via TEE_keybox_load() and only ever
 * returns the public key or raw r||s signatures to clients.
 */

#include <string.h>

#include <tee_internal_api.h>
#include <tee_ta_api.h>

#include <ecdsa_sign_ta.h>

#define ECDSA_SIGN_KEYBOX_KEY_NAME "ecc_key"
#define ECDSA_SIGN_KEYPAIR_BLOB_SIZE 96u

typedef struct
{
    TEE_ObjectHandle key_object;
} EcdsaSignSessionContext_t;

static TEE_Result load_keypair_from_keybox(EcdsaSignSessionContext_t *context)
{
    TEE_Result res;
    uint8_t keypair_blob[ECDSA_SIGN_KEYPAIR_BLOB_SIZE] = {0};
    TEE_Attribute attrs[4];
    TEE_ObjectHandle key_object = TEE_HANDLE_NULL;

    res = TEE_keybox_load(ECDSA_SIGN_KEYBOX_KEY_NAME, keypair_blob,
                          sizeof(keypair_blob));
    if (res != TEE_SUCCESS)
    {
        return TEE_ERROR_ITEM_NOT_FOUND;
    }
    res = TEE_AllocateTransientObject(TEE_TYPE_ECDSA_KEYPAIR, 256u,
                                      &key_object);
    if (res != TEE_SUCCESS)
    {
        return TEE_ERROR_OUT_OF_MEMORY;
    }
    TEE_InitRefAttribute(&attrs[0], TEE_ATTR_ECC_PRIVATE_VALUE,
                         keypair_blob, ECDSA_P256_PRIVATE_KEY_SIZE);
    TEE_InitRefAttribute(&attrs[1], TEE_ATTR_ECC_PUBLIC_VALUE_X,
                         keypair_blob + ECDSA_P256_PRIVATE_KEY_SIZE,
                         ECDSA_P256_PRIVATE_KEY_SIZE);
    TEE_InitRefAttribute(&attrs[2], TEE_ATTR_ECC_PUBLIC_VALUE_Y,
                         keypair_blob + 2u * ECDSA_P256_PRIVATE_KEY_SIZE,
                         ECDSA_P256_PRIVATE_KEY_SIZE);
    TEE_InitValueAttribute(&attrs[3], TEE_ATTR_ECC_CURVE,
                           TEE_ECC_CURVE_NIST_P256, 0u);
    res = TEE_PopulateTransientObject(key_object, attrs, 4u);
    memset(keypair_blob, 0, sizeof(keypair_blob));
    if (res != TEE_SUCCESS)
    {
        TEE_FreeTransientObject(key_object);
        return TEE_ERROR_BAD_FORMAT;
    }
    context->key_object = key_object;
    return TEE_SUCCESS;
}

TEE_Result TA_CreateEntryPoint(void)
{
    return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void)
{
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t n_param_types,
                                    TEE_Param params[4],
                                    void **session_context)
{
    EcdsaSignSessionContext_t *context;

    (void)n_param_types;
    (void)params;
    if (session_context == NULL)
    {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    context = TEE_Malloc(sizeof(*context), TEE_MALLOC_FILL_ZERO);
    if (context == NULL)
    {
        return TEE_ERROR_OUT_OF_MEMORY;
    }
    if (load_keypair_from_keybox(context) != TEE_SUCCESS)
    {
        TEE_Free(context);
        return TEE_ERROR_ITEM_NOT_FOUND;
    }
    *session_context = context;
    return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *session_context)
{
    EcdsaSignSessionContext_t *context = session_context;

    if (context != NULL)
    {
        TEE_FreeTransientObject(context->key_object);
        TEE_Free(context);
    }
}

static TEE_Result get_public_key(EcdsaSignSessionContext_t *context,
                                 TEE_Param params[4])
{
    TEE_Result res;
    uint32_t size = ECDSA_P256_PUBLIC_KEY_SIZE;

    if (params[0].memref.size < ECDSA_P256_PUBLIC_KEY_SIZE ||
        params[0].memref.buffer == NULL)
    {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    res = TEE_GetObjectBufferAttribute(context->key_object,
                                       TEE_ATTR_ECC_PUBLIC_VALUE_X,
                                       params[0].memref.buffer, &size);
    if (res != TEE_SUCCESS || size != ECDSA_P256_PRIVATE_KEY_SIZE)
    {
        return TEE_ERROR_GENERIC;
    }
    res = TEE_GetObjectBufferAttribute(context->key_object,
                                       TEE_ATTR_ECC_PUBLIC_VALUE_Y,
                                       (uint8_t *)params[0].memref.buffer +
                                           ECDSA_P256_PRIVATE_KEY_SIZE,
                                       &size);
    if (res != TEE_SUCCESS || size != ECDSA_P256_PRIVATE_KEY_SIZE)
    {
        return TEE_ERROR_GENERIC;
    }
    params[0].memref.size = ECDSA_P256_PUBLIC_KEY_SIZE;
    return TEE_SUCCESS;
}

static TEE_Result sign_digest(EcdsaSignSessionContext_t *context,
                              TEE_Param params[4])
{
    TEE_Result res;
    TEE_OperationHandle operation = TEE_HANDLE_NULL;
    uint32_t signature_size = ECDSA_P256_SIGNATURE_SIZE;

    if (params[0].memref.size != ECDSA_P256_DIGEST_SIZE ||
        params[0].memref.buffer == NULL ||
        params[1].memref.size < ECDSA_P256_SIGNATURE_SIZE ||
        params[1].memref.buffer == NULL)
    {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    res = TEE_AllocateOperation(&operation, TEE_ALG_ECDSA_P256,
                                TEE_MODE_SIGN, 256u);
    if (res != TEE_SUCCESS)
    {
        return res;
    }
    res = TEE_SetOperationKey(operation, context->key_object);
    if (res == TEE_SUCCESS)
    {
        res = TEE_AsymmetricSignDigest(operation, NULL, 0u,
                                       params[0].memref.buffer,
                                       params[0].memref.size,
                                       params[1].memref.buffer,
                                       &signature_size);
    }
    TEE_FreeOperation(operation);
    if (res != TEE_SUCCESS)
    {
        return res;
    }
    params[1].memref.size = signature_size;
    return TEE_SUCCESS;
}

TEE_Result TA_InvokeCommandEntryPoint(void *session_context,
                                      uint32_t command_id,
                                      uint32_t n_param_types,
                                      TEE_Param params[4])
{
    EcdsaSignSessionContext_t *context = session_context;

    (void)n_param_types;
    if (context == NULL)
    {
        return TEE_ERROR_BAD_STATE;
    }
    switch (command_id)
    {
    case ECDSA_SIGN_CMD_GET_PUBLIC_KEY:
        return get_public_key(context, params);

    case ECDSA_SIGN_CMD_SIGN_DIGEST:
        return sign_digest(context, params);

    default:
        return TEE_ERROR_BAD_PARAMETERS;
    }
}
