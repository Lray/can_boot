#ifndef TOKEN_SIGNER_CLIENT_H
#define TOKEN_SIGNER_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "token_signer_codec.h"
#include "token_signer_protocol.h"

#define TOKEN_SIGNER_DEFAULT_ENDPOINT "/run/ecu-token-signer/v1.sock"
#define TOKEN_SIGNER_DEFAULT_TIMEOUT_MS 1000u

#define TOKEN_SIGNER_ERR_SOCKET_POLICY (-1203)
#define TOKEN_SIGNER_ERR_CONNECT (-1204)
#define TOKEN_SIGNER_ERR_PEER (-1205)
#define TOKEN_SIGNER_ERR_IO (-1206)

typedef struct
{
    const char *endpoint;
    uid_t expected_peer_uid;
    gid_t expected_peer_gid;
    uid_t expected_socket_uid;
    gid_t expected_socket_gid;
    mode_t expected_socket_mode;
    uint32_t timeout_ms;
} TokenSignerClient_t;

int token_signer_client_issue(const TokenSignerClient_t *client,
                              const uint8_t *seed, size_t seed_len,
                              uint8_t *token_out, size_t token_cap,
                              size_t *token_len_out);

#endif
