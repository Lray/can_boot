#ifndef TOKEN_SIGNER_CODEC_H
#define TOKEN_SIGNER_CODEC_H

#include <stddef.h>
#include <stdint.h>

#define TOKEN_SIGNER_ERR_INVALID_ARG (-1201)
#define TOKEN_SIGNER_ERR_RANDOM (-1202)
#define TOKEN_SIGNER_ERR_RESPONSE (-1207)
#define TOKEN_SIGNER_ERR_REMOTE (-1208)

int token_signer_codec_encode_request(const uint8_t *seed, size_t seed_len,
                                      uint8_t request_id[16], uint8_t *packet,
                                      size_t packet_cap, size_t *packet_len_out);
int token_signer_codec_decode_response(const uint8_t *packet, size_t packet_len,
                                       const uint8_t expected_request_id[16],
                                       uint8_t *token_out, size_t token_cap,
                                       size_t *token_len_out);

#endif
