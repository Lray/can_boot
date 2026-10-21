#include "security_token_tee.h"

#include <stdlib.h>
#include <string.h>

#include <tee_client_api.h>

#include "ta_api.h"
#include "shared/security_token_profile.h"
#include "shared/uds_protocol.h"

struct SecurityTokenTee
{
    TEEC_Context context;
    TEEC_Session session;
};

static const uint8_t expected_public_key[TOKEN_TA_PUBLIC_KEY_SIZE] = {
    0x9A, 0x57, 0x4E, 0x42, 0x9B, 0x51, 0xD2, 0x01,
    0x21, 0x71, 0x16, 0x37, 0xD8, 0x31, 0xA4, 0x95,
    0xE8, 0x1C, 0x2A, 0x8F, 0x8D, 0x49, 0xC5, 0x6A,
    0x61, 0x89, 0x28, 0xD5, 0x84, 0xA4, 0xB9, 0xD3,
    0x22, 0x32, 0x5C, 0x84, 0x5E, 0xD9, 0xEE, 0x46,
    0x3F, 0x25, 0xEA, 0xEA, 0x00, 0x8C, 0x94, 0x11,
    0x65, 0x9D, 0xC3, 0xB7, 0x77, 0x29, 0xFB, 0xB0,
    0x3D, 0x84, 0x4C, 0xD8, 0xF2, 0xF5, 0x1A, 0x80,
};

static int verify_public_key(SecurityTokenTee_t *tee)
{
    TEEC_Operation operation = {0};
    uint8_t public_key[TOKEN_TA_PUBLIC_KEY_SIZE] = {0};
    TEEC_Result result;

    operation.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT,
                                             TEEC_NONE, TEEC_NONE, TEEC_NONE);
    operation.params[0].tmpref.buffer = public_key;
    operation.params[0].tmpref.size = sizeof(public_key);
    result = TEEC_InvokeCommand(&tee->session,
                                TOKEN_TA_CMD_GET_PUBLIC_KEY,
                                &operation, NULL);
    return result == TEEC_SUCCESS &&
                   operation.params[0].tmpref.size == sizeof(public_key) &&
                   memcmp(public_key, expected_public_key,
                          sizeof(public_key)) == 0
               ? 0
               : -1;
}

int security_token_tee_open(SecurityTokenTee_t **tee_out)
{
    TEEC_UUID uuid = TOKEN_TA_UUID;
    SecurityTokenTee_t *tee;

    if (tee_out == NULL)
    {
        return -1;
    }
    *tee_out = NULL;
    tee = calloc(1u, sizeof(*tee));
    if (tee == NULL)
    {
        return -1;
    }
    if (TEEC_InitializeContext(NULL, &tee->context) != TEEC_SUCCESS)
    {
        free(tee);
        return -1;
    }
    if (TEEC_OpenSession(&tee->context, &tee->session, &uuid,
                         TEEC_LOGIN_PUBLIC, NULL, NULL, NULL) != TEEC_SUCCESS)
    {
        TEEC_FinalizeContext(&tee->context);
        free(tee);
        return -1;
    }
    if (verify_public_key(tee) != 0)
    {
        security_token_tee_close(tee);
        return -1;
    }
    *tee_out = tee;
    return 0;
}

void security_token_tee_close(SecurityTokenTee_t *tee)
{
    if (tee != NULL)
    {
        TEEC_CloseSession(&tee->session);
        TEEC_FinalizeContext(&tee->context);
        free(tee);
    }
}

int security_token_tee_issue(SecurityTokenTee_t *tee,
                             const uint8_t seed[SECURITY_ACCESS_SEED_SIZE],
                             uint8_t *token_out, size_t token_cap,
                             size_t *token_len_out)
{
    TEEC_Operation operation = {0};
    TEEC_Result result;

    if (tee == NULL || seed == NULL || token_out == NULL ||
        token_cap < SECURITY_TOKEN_MAX_SIZE || token_len_out == NULL)
    {
        return -1;
    }
    *token_len_out = 0u;
    operation.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
                                             TEEC_MEMREF_TEMP_OUTPUT,
                                             TEEC_NONE, TEEC_NONE);
    operation.params[0].tmpref.buffer = (void *)seed;
    operation.params[0].tmpref.size = SECURITY_ACCESS_SEED_SIZE;
    operation.params[1].tmpref.buffer = token_out;
    operation.params[1].tmpref.size = token_cap;
    result = TEEC_InvokeCommand(&tee->session,
                                TOKEN_TA_CMD_ISSUE_TOKEN,
                                &operation, NULL);
    if (result != TEEC_SUCCESS || operation.params[1].tmpref.size == 0u ||
        operation.params[1].tmpref.size > SECURITY_TOKEN_MAX_SIZE)
    {
        return -1;
    }
    *token_len_out = operation.params[1].tmpref.size;
    return 0;
}
