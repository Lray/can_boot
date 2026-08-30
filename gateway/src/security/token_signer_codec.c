#include "token_signer_codec.h"
#include "token_signer_protocol.h"

#include <errno.h>
#include <string.h>
#include <sys/random.h>

#include "profile.h"
#include "qcbor/qcbor_encode.h"
#include "qcbor/qcbor_spiffy_decode.h"

static int fill_random(uint8_t *output, size_t length)
{
    size_t complete = 0u;

    while (complete < length)
    {
        ssize_t count = getrandom(output + complete, length - complete, 0);

        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count <= 0)
        {
            return -1;
        }
        complete += (size_t)count;
    }
    return 0;
}

int token_signer_codec_encode_request(const uint8_t *seed, size_t seed_len,
                                      uint8_t request_id[16], uint8_t *packet,
                                      size_t packet_cap, size_t *packet_len_out)
{
    QCBOREncodeContext context;
    UsefulBufC encoded = NULLUsefulBufC;

    if (seed == NULL || request_id == NULL || packet == NULL || packet_len_out == NULL ||
        seed_len == 0u || seed_len > SECURITY_ACCESS_SEED_MAX_SIZE)
    {
        return TOKEN_SIGNER_ERR_INVALID_ARG;
    }
    if (fill_random(request_id, 16u) != 0)
    {
        return TOKEN_SIGNER_ERR_RANDOM;
    }
    QCBOREncode_Init(&context, (UsefulBuf){packet, packet_cap});
    QCBOREncode_OpenArray(&context);
    QCBOREncode_AddBytes(&context, (UsefulBufC){request_id, 16u});
    QCBOREncode_AddBytes(&context, (UsefulBufC){seed, seed_len});
    QCBOREncode_AddUInt64(&context, seed_len);
    QCBOREncode_CloseArray(&context);
    if (QCBOREncode_Finish(&context, &encoded) != QCBOR_SUCCESS)
    {
        return TOKEN_SIGNER_ERR_INVALID_ARG;
    }
    *packet_len_out = encoded.len;
    return 0;
}

int token_signer_codec_decode_response(const uint8_t *packet, size_t packet_len,
                                       const uint8_t expected_request_id[16],
                                       uint8_t *token_out, size_t token_cap,
                                       size_t *token_len_out)
{
    QCBORDecodeContext context;
    QCBORItem array;
    UsefulBufC request_id = NULLUsefulBufC;
    UsefulBufC token = NULLUsefulBufC;
    uint64_t status = TOKEN_SIGNER_RESPONSE_STATUS_MAX + 1u;

    if (packet == NULL || expected_request_id == NULL || token_out == NULL ||
        token_len_out == NULL || token_cap == 0u)
    {
        return TOKEN_SIGNER_ERR_INVALID_ARG;
    }
    *token_len_out = 0u;
    QCBORDecode_Init(&context, (UsefulBufC){packet, packet_len}, QCBOR_DECODE_MODE_NORMAL);
    QCBORDecode_EnterArray(&context, &array);
    QCBORDecode_GetUInt64(&context, &status);
    QCBORDecode_GetByteString(&context, &request_id);
    if (status == TOKEN_SIGNER_RESPONSE_STATUS_OK)
    {
        QCBORDecode_GetByteString(&context, &token);
    }
    QCBORDecode_ExitArray(&context);
    if (QCBORDecode_Finish(&context) != QCBOR_SUCCESS ||
        ((status == TOKEN_SIGNER_RESPONSE_STATUS_OK && array.val.uCount != 3u) ||
         (status != TOKEN_SIGNER_RESPONSE_STATUS_OK && array.val.uCount != 2u)) ||
        status > TOKEN_SIGNER_RESPONSE_STATUS_MAX ||
        !token_signer_buffer_equals(request_id.ptr, request_id.len,
                                    expected_request_id, 16u))
    {
        return TOKEN_SIGNER_ERR_RESPONSE;
    }
    if (status != TOKEN_SIGNER_RESPONSE_STATUS_OK)
    {
        return TOKEN_SIGNER_ERR_REMOTE;
    }
    if (token.ptr == NULL || token.len == 0u || token.len > token_cap ||
        token.len > SECURITY_ACCESS_TOKEN_MAX_SIZE)
    {
        return TOKEN_SIGNER_ERR_RESPONSE;
    }
    memcpy(token_out, token.ptr, token.len);
    *token_len_out = token.len;
    return 0;
}
