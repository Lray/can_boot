#include <string.h>

#include <tee_internal_api.h>
#include <tee_ta_api.h>

#include "ta_api.h"
#include "token_builder.h"
#include "shared/security_token_profile.h"
#include "shared/uds_protocol.h"

#define KEYBOX_KEY_NAME "ecc_key"
#define KEYPAIR_BLOB_SIZE 96u

typedef struct
{
    TEE_ObjectHandle key_object;
} SecurityTokenSessionContext_t;

static TEE_Result load_keypair(SecurityTokenSessionContext_t *context)
{
    TEE_Result result;
    uint8_t keypair_blob[KEYPAIR_BLOB_SIZE] = {0};
    TEE_Attribute attributes[4];
    TEE_ObjectHandle key_object = TEE_HANDLE_NULL;

    result = TEE_keybox_load(KEYBOX_KEY_NAME, keypair_blob,
                             sizeof(keypair_blob));
    if (result != TEE_SUCCESS)
    {
        return TEE_ERROR_ITEM_NOT_FOUND;
    }
    result = TEE_AllocateTransientObject(TEE_TYPE_ECDSA_KEYPAIR, 256u,
                                         &key_object);
    if (result != TEE_SUCCESS)
    {
        memset(keypair_blob, 0, sizeof(keypair_blob));
        return TEE_ERROR_OUT_OF_MEMORY;
    }
    TEE_InitRefAttribute(&attributes[0], TEE_ATTR_ECC_PRIVATE_VALUE,
                         keypair_blob, TOKEN_TA_PRIVATE_KEY_SIZE);
    TEE_InitRefAttribute(&attributes[1], TEE_ATTR_ECC_PUBLIC_VALUE_X,
                         keypair_blob + TOKEN_TA_PRIVATE_KEY_SIZE,
                         TOKEN_TA_PRIVATE_KEY_SIZE);
    TEE_InitRefAttribute(&attributes[2], TEE_ATTR_ECC_PUBLIC_VALUE_Y,
                         keypair_blob + 2u * TOKEN_TA_PRIVATE_KEY_SIZE,
                         TOKEN_TA_PRIVATE_KEY_SIZE);
    TEE_InitValueAttribute(&attributes[3], TEE_ATTR_ECC_CURVE,
                           TEE_ECC_CURVE_NIST_P256, 0u);
    result = TEE_PopulateTransientObject(key_object, attributes, 4u);
    memset(keypair_blob, 0, sizeof(keypair_blob));
    if (result != TEE_SUCCESS)
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

TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types, TEE_Param params[4],
                                    void **session_context)
{
    SecurityTokenSessionContext_t *context;

    (void)params;
    if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE,
                                       TEE_PARAM_TYPE_NONE,
                                       TEE_PARAM_TYPE_NONE,
                                       TEE_PARAM_TYPE_NONE) ||
        session_context == NULL)
    {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    context = TEE_Malloc(sizeof(*context), TEE_MALLOC_FILL_ZERO);
    if (context == NULL)
    {
        return TEE_ERROR_OUT_OF_MEMORY;
    }
    if (load_keypair(context) != TEE_SUCCESS)
    {
        TEE_Free(context);
        return TEE_ERROR_ITEM_NOT_FOUND;
    }
    *session_context = context;
    return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *session_context)
{
    SecurityTokenSessionContext_t *context = session_context;

    if (context != NULL)
    {
        TEE_FreeTransientObject(context->key_object);
        TEE_Free(context);
    }
}

static TEE_Result get_public_key(SecurityTokenSessionContext_t *context,
                                 TEE_Param params[4])
{
    TEE_Result result;
    uint32_t coordinate_size = TOKEN_TA_PRIVATE_KEY_SIZE;

    if (params[0].memref.buffer == NULL ||
        params[0].memref.size < TOKEN_TA_PUBLIC_KEY_SIZE)
    {
        params[0].memref.size = TOKEN_TA_PUBLIC_KEY_SIZE;
        return TEE_ERROR_SHORT_BUFFER;
    }
    result = TEE_GetObjectBufferAttribute(context->key_object,
                                           TEE_ATTR_ECC_PUBLIC_VALUE_X,
                                           params[0].memref.buffer,
                                           &coordinate_size);
    if (result != TEE_SUCCESS ||
        coordinate_size != TOKEN_TA_PRIVATE_KEY_SIZE)
    {
        return TEE_ERROR_GENERIC;
    }
    coordinate_size = TOKEN_TA_PRIVATE_KEY_SIZE;
    result = TEE_GetObjectBufferAttribute(
        context->key_object, TEE_ATTR_ECC_PUBLIC_VALUE_Y,
        (uint8_t *)params[0].memref.buffer + TOKEN_TA_PRIVATE_KEY_SIZE,
        &coordinate_size);
    if (result != TEE_SUCCESS ||
        coordinate_size != TOKEN_TA_PRIVATE_KEY_SIZE)
    {
        return TEE_ERROR_GENERIC;
    }
    params[0].memref.size = TOKEN_TA_PUBLIC_KEY_SIZE;
    return TEE_SUCCESS;
}

static TEE_Result issue_token(SecurityTokenSessionContext_t *context,
                              TEE_Param params[4])
{
    const uint8_t *seed = params[0].memref.buffer;
    struct t_cose_key signing_key = T_COSE_NULL_KEY;
    size_t token_size = params[1].memref.size;
    uint8_t seed_or = 0u;
    size_t index;

    if (seed == NULL || params[0].memref.size != SECURITY_ACCESS_SEED_SIZE ||
        params[1].memref.buffer == NULL)
    {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    if (params[1].memref.size < SECURITY_TOKEN_MAX_SIZE)
    {
        params[1].memref.size = SECURITY_TOKEN_MAX_SIZE;
        return TEE_ERROR_SHORT_BUFFER;
    }
    for (index = 0u; index < SECURITY_ACCESS_SEED_SIZE; ++index)
    {
        seed_or |= seed[index];
    }
    if (seed_or == 0u)
    {
        return TEE_ERROR_BAD_PARAMETERS;
    }
    signing_key.crypto_lib = T_COSE_CRYPTO_LIB_UNIDENTIFIED;
    signing_key.k.key_ptr = context->key_object;
    if (token_build(signing_key, seed, params[1].memref.buffer,
                    params[1].memref.size, &token_size) != TOKEN_BUILD_OK)
    {
        return TEE_ERROR_GENERIC;
    }
    params[1].memref.size = token_size;
    return TEE_SUCCESS;
}

TEE_Result TA_InvokeCommandEntryPoint(void *session_context,
                                      uint32_t command_id,
                                      uint32_t param_types,
                                      TEE_Param params[4])
{
    SecurityTokenSessionContext_t *context = session_context;

    if (context == NULL)
    {
        return TEE_ERROR_BAD_STATE;
    }
    if (command_id == TOKEN_TA_CMD_GET_PUBLIC_KEY &&
        param_types == TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                       TEE_PARAM_TYPE_NONE,
                                       TEE_PARAM_TYPE_NONE,
                                       TEE_PARAM_TYPE_NONE))
    {
        return get_public_key(context, params);
    }
    if (command_id == TOKEN_TA_CMD_ISSUE_TOKEN &&
        param_types == TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                       TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                       TEE_PARAM_TYPE_NONE,
                                       TEE_PARAM_TYPE_NONE))
    {
        return issue_token(context, params);
    }
    return TEE_ERROR_BAD_PARAMETERS;
}
