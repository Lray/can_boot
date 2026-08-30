#include "security_token_cose.h"

#include "security_crypto_port.h"

#include <stddef.h>
#include <string.h>

#include "qcbor/qcbor_spiffy_decode.h"
#include "t_cose/t_cose_sign1_verify.h"
#include "t_cose_standard_constants.h"

static bool SecurityToken_DecodeClaims(struct q_useful_buf_c payload,
                                        security_token_claims_t *claims)
{
    QCBORDecodeContext context;
    UsefulBufC seed_challenge = NULLUsefulBufC;
    uint64_t freshness_nonce = 0U;

    if ((payload.ptr == 0) || (claims == 0))
    {
        return false;
    }

    QCBORDecode_Init(&context,
                     (UsefulBufC){payload.ptr, payload.len},
                     QCBOR_DECODE_MODE_NORMAL);
    QCBORDecode_EnterMap(&context, 0);
    QCBORDecode_GetByteStringInMapN(&context,
                                    SECURITY_TOKEN_LABEL_SEED_CHALLENGE,
                                    &seed_challenge);
    QCBORDecode_GetUInt64InMapN(&context,
                                SECURITY_TOKEN_LABEL_FRESHNESS_NONCE,
                                &freshness_nonce);
    QCBORDecode_ExitMap(&context);

    if ((QCBORDecode_Finish(&context) != QCBOR_SUCCESS) ||
        (seed_challenge.len != SECURITY_ACCESS_SEED_SIZE))
    {
        return false;
    }

    memcpy(claims->seed_challenge,
           seed_challenge.ptr,
           SECURITY_ACCESS_SEED_SIZE);
    claims->freshness_nonce = freshness_nonce;
    return true;
}

static security_token_result_t SecurityToken_MapTCoseError(
    enum t_cose_err_t error)
{
    switch (error)
    {
    case T_COSE_SUCCESS:
        return SECURITY_TOKEN_RESULT_OK;

    case T_COSE_ERR_SIG_VERIFY:
    case T_COSE_ERR_TAMPERING_DETECTED:
        return SECURITY_TOKEN_RESULT_BAD_SIGNATURE;

    case T_COSE_ERR_UNKNOWN_KEY:
    case T_COSE_ERR_NO_KID:
    case T_COSE_ERR_WRONG_TYPE_OF_KEY:
        return SECURITY_TOKEN_RESULT_UNSUPPORTED_KEY;

    case T_COSE_ERR_UNSUPPORTED_SIGNING_ALG:
    case T_COSE_ERR_NO_ALG_ID:
        return SECURITY_TOKEN_RESULT_UNSUPPORTED_TOKEN;

    default:
        return SECURITY_TOKEN_RESULT_BAD_FORMAT;
    }
}

security_token_result_t SecurityToken_CoseVerify(
    const uint8_t *token,
    size_t token_length,
    security_token_claims_t *claims)
{
    struct t_cose_sign1_verify_ctx verify_context;
    struct t_cose_parameters parameters;
    struct q_useful_buf_c payload = NULL_Q_USEFUL_BUF_C;
    struct q_useful_buf_c sign1;
    struct t_cose_key verification_key = T_COSE_NULL_KEY;
    const uint8_t *public_key = 0;
    size_t public_key_length = 0U;
    enum t_cose_err_t cose_result;

    if ((token == 0) || (token_length == 0U) || (claims == 0))
    {
        return SECURITY_TOKEN_RESULT_BAD_FORMAT;
    }

    memset(claims, 0, sizeof(*claims));
    if (!SecurityCrypto_GetProvisionedPublicKey(&public_key,
                                                &public_key_length) ||
        (public_key == 0) || (public_key_length != 64U))
    {
        return SECURITY_TOKEN_RESULT_UNSUPPORTED_KEY;
    }

    sign1.ptr = token;
    sign1.len = token_length;
    verification_key.crypto_lib = T_COSE_CRYPTO_LIB_UNIDENTIFIED;
    verification_key.k.key_ptr = (void *)public_key;

    t_cose_sign1_verify_init(&verify_context, 0U);
    t_cose_sign1_set_verification_key(&verify_context, verification_key);
    cose_result = t_cose_sign1_verify(&verify_context,
                                      sign1,
                                      &payload,
                                      &parameters);
    if (cose_result != T_COSE_SUCCESS)
    {
        return SecurityToken_MapTCoseError(cose_result);
    }

    if (parameters.cose_algorithm_id != COSE_ALGORITHM_ES256)
    {
        return SECURITY_TOKEN_RESULT_UNSUPPORTED_KEY;
    }

    if (!SecurityToken_DecodeClaims(payload, claims))
    {
        return SECURITY_TOKEN_RESULT_BAD_FORMAT;
    }

    claims->signature_valid = true;
    return SECURITY_TOKEN_RESULT_OK;
}
