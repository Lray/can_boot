#include "token_builder.h"

#include "profile.h"
#include "qcbor/qcbor_encode.h"
#include "shared/security_token_profile.h"
#include "t_cose/t_cose_sign1_sign.h"
#include "t_cose_standard_constants.h"

#define TOKEN_BUILDER_PAYLOAD_MAX_SIZE 256u

static uint64_t freshness_nonce(const uint8_t *seed, size_t seed_len)
{
    uint64_t value = 0u;
    size_t index;

    for (index = 0u; index < seed_len; ++index)
    {
        value = (value << 8) | seed[index];
    }
    return (value << 32) | 1u;
}

TokenBuilderResult_t token_builder_sign_seed(
    struct t_cose_key signing_key, const uint8_t *seed, size_t seed_len,
    uint8_t *token_out, size_t token_cap, size_t *token_len_out)
{
    QCBOREncodeContext payload_context;
    uint8_t payload_buffer[TOKEN_BUILDER_PAYLOAD_MAX_SIZE] = {0};
    UsefulBufC payload = NULLUsefulBufC;
    struct t_cose_sign1_sign_ctx sign_context;
    struct q_useful_buf_c token = NULL_Q_USEFUL_BUF_C;
    enum t_cose_err_t cose_rc;

    if (seed == NULL || seed_len != SECURITY_ACCESS_SEED_SIZE ||
        token_out == NULL || token_cap == 0u || token_len_out == NULL)
    {
        return TOKEN_BUILDER_INVALID_ARGUMENT;
    }
    *token_len_out = 0u;
    QCBOREncode_Init(&payload_context,
                     (UsefulBuf){payload_buffer, sizeof(payload_buffer)});
    QCBOREncode_OpenMap(&payload_context);
    QCBOREncode_AddBytesToMapN(&payload_context,
                               SECURITY_TOKEN_LABEL_SEED_CHALLENGE,
                               (UsefulBufC){seed, seed_len});
    QCBOREncode_AddUInt64ToMapN(&payload_context,
                                SECURITY_TOKEN_LABEL_FRESHNESS_NONCE,
                                freshness_nonce(seed, seed_len));
    QCBOREncode_CloseMap(&payload_context);
    if (QCBOREncode_Finish(&payload_context, &payload) != QCBOR_SUCCESS)
    {
        return TOKEN_BUILDER_TOO_LARGE;
    }

    t_cose_sign1_sign_init(&sign_context, T_COSE_OPT_OMIT_CBOR_TAG,
                           T_COSE_ALGORITHM_ES256);
    t_cose_sign1_set_signing_key(&sign_context, signing_key,
                                 NULL_Q_USEFUL_BUF_C);
    cose_rc = t_cose_sign1_sign(
        &sign_context, (struct q_useful_buf_c){payload.ptr, payload.len},
        (struct q_useful_buf){token_out, token_cap}, &token);
    if (cose_rc != T_COSE_SUCCESS)
    {
        return cose_rc == T_COSE_ERR_TOO_SMALL ? TOKEN_BUILDER_TOO_LARGE
                                               : TOKEN_BUILDER_SIGNING_FAILED;
    }
    if (token.len == 0u || token.len > SECURITY_TOKEN_MAX_SIZE)
    {
        return TOKEN_BUILDER_TOO_LARGE;
    }
    *token_len_out = token.len;
    return TOKEN_BUILDER_OK;
}
