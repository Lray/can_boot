#include "token_signer_daemon.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "qcbor/qcbor_encode.h"
#include "qcbor/qcbor_spiffy_decode.h"
#include "t_cose/t_cose_sign1_sign.h"
#include "t_cose_standard_constants.h"
#include "t_cose_crypto.h"
#include "profile.h"
#include "token_signer_protocol.h"
#include "shared/security_token_profile.h"

#define TOKEN_SIGNER_DAEMON_SOCKET_MODE 0660u
#define TOKEN_SIGNER_DAEMON_BACKLOG 8u
#define TOKEN_SIGNER_DAEMON_POLL_TIMEOUT_MS 5000u
#define TOKEN_SIGNER_TOKEN_MAX_SIZE SECURITY_TOKEN_MAX_SIZE
#define TOKEN_SIGNER_PAYLOAD_MAX_SIZE 256u
#define TOKEN_SIGNER_SEED_SIZE SECURITY_ACCESS_SEED_SIZE

typedef struct
{
    uint8_t request_id[16];
    uint8_t seed[64];
    size_t seed_len;
} SignerRequest_t;

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

static int parse_request(const uint8_t *packet, size_t packet_len,
                         SignerRequest_t *request)
{
    QCBORDecodeContext context;
    QCBORItem array;
    UsefulBufC request_id = NULLUsefulBufC;
    UsefulBufC seed = NULLUsefulBufC;
    uint64_t seed_len = 0u;

    if (packet == NULL || request == NULL)
    {
        return TOKEN_SIGNER_RESPONSE_STATUS_INTERNAL;
    }
    memset(request, 0, sizeof(*request));
    QCBORDecode_Init(&context, (UsefulBufC){packet, packet_len},
                     QCBOR_DECODE_MODE_NORMAL);
    QCBORDecode_EnterArray(&context, &array);
    QCBORDecode_GetByteString(&context, &request_id);
    QCBORDecode_GetByteString(&context, &seed);
    QCBORDecode_GetUInt64(&context, &seed_len);
    QCBORDecode_ExitArray(&context);
    if (QCBORDecode_Finish(&context) != QCBOR_SUCCESS ||
        array.val.uCount != 3u ||
        request_id.len != 16u || seed.ptr == NULL ||
        seed.len != TOKEN_SIGNER_SEED_SIZE || seed_len != seed.len)
    {
        return TOKEN_SIGNER_RESPONSE_STATUS_MALFORMED;
    }
    memcpy(request->request_id, request_id.ptr, sizeof(request->request_id));
    memcpy(request->seed, seed.ptr, seed.len);
    request->seed_len = seed.len;
    return TOKEN_SIGNER_RESPONSE_STATUS_OK;
}

static int build_token(TokenSignerTee_t *tee, const SignerRequest_t *request,
                       uint8_t *token_out, size_t token_cap,
                       size_t *token_len_out)
{
    QCBOREncodeContext payload_context;
    uint8_t payload_buffer[TOKEN_SIGNER_PAYLOAD_MAX_SIZE] = {0};
    UsefulBufC payload = NULLUsefulBufC;
    struct t_cose_sign1_sign_ctx sign_context;
    struct t_cose_key signing_key = T_COSE_NULL_KEY;
    struct q_useful_buf_c token = NULL_Q_USEFUL_BUF_C;
    enum t_cose_err_t cose_rc;

    if (tee == NULL || request == NULL || token_out == NULL ||
        token_len_out == NULL || request->seed_len != TOKEN_SIGNER_SEED_SIZE)
    {
        return TOKEN_SIGNER_RESPONSE_STATUS_INTERNAL;
    }
    QCBOREncode_Init(&payload_context,
                     (UsefulBuf){payload_buffer, sizeof(payload_buffer)});
    QCBOREncode_OpenMap(&payload_context);
    QCBOREncode_AddBytesToMapN(&payload_context, SECURITY_TOKEN_LABEL_SEED_CHALLENGE,
                               (UsefulBufC){request->seed, request->seed_len});
    QCBOREncode_AddUInt64ToMapN(
        &payload_context, SECURITY_TOKEN_LABEL_FRESHNESS_NONCE,
        freshness_nonce(request->seed, request->seed_len));
    QCBOREncode_CloseMap(&payload_context);
    if (QCBOREncode_Finish(&payload_context, &payload) != QCBOR_SUCCESS)
    {
        return TOKEN_SIGNER_RESPONSE_STATUS_TOO_LARGE;
    }
    signing_key.k.key_ptr = tee;
    t_cose_sign1_sign_init(&sign_context, T_COSE_OPT_OMIT_CBOR_TAG,
                           T_COSE_ALGORITHM_ES256);
    t_cose_sign1_set_signing_key(&sign_context, signing_key,
                                 NULL_Q_USEFUL_BUF_C);
    cose_rc = t_cose_sign1_sign(
        &sign_context, (struct q_useful_buf_c){payload.ptr, payload.len},
        (struct q_useful_buf){token_out, token_cap}, &token);
    if (cose_rc != T_COSE_SUCCESS)
    {
        return cose_rc == T_COSE_ERR_TOO_SMALL
                   ? TOKEN_SIGNER_RESPONSE_STATUS_TOO_LARGE
                   : TOKEN_SIGNER_RESPONSE_STATUS_SIGNING;
    }
    if (token.len == 0u || token.len > TOKEN_SIGNER_TOKEN_MAX_SIZE)
    {
        return TOKEN_SIGNER_RESPONSE_STATUS_TOO_LARGE;
    }
    *token_len_out = token.len;
    return TOKEN_SIGNER_RESPONSE_STATUS_OK;
}

static int encode_response(unsigned int status, const uint8_t request_id[16],
                           const uint8_t *token, size_t token_len,
                           uint8_t *response, size_t response_cap,
                           size_t *response_len_out)
{
    QCBOREncodeContext context;
    UsefulBufC encoded = NULLUsefulBufC;

    if (request_id == NULL || response == NULL || response_len_out == NULL ||
        (status == TOKEN_SIGNER_RESPONSE_STATUS_OK &&
         (token == NULL || token_len == 0u)))
    {
        return -1;
    }
    QCBOREncode_Init(&context, (UsefulBuf){response, response_cap});
    QCBOREncode_OpenArray(&context);
    QCBOREncode_AddUInt64(&context, status);
    QCBOREncode_AddBytes(&context, (UsefulBufC){request_id, 16u});
    if (status == TOKEN_SIGNER_RESPONSE_STATUS_OK)
    {
        QCBOREncode_AddBytes(&context, (UsefulBufC){token, token_len});
    }
    QCBOREncode_CloseArray(&context);
    if (QCBOREncode_Finish(&context, &encoded) != QCBOR_SUCCESS)
    {
        return -1;
    }
    *response_len_out = encoded.len;
    return 0;
}

static int peer_is_authorized(int client_fd,
                              const TokenSignerDaemonPolicy_t *policy)
{
    struct ucred peer;
    socklen_t peer_len = sizeof(peer);

    return getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &peer, &peer_len) ==
                   0 &&
                   peer_len == sizeof(peer) && peer.uid == policy->client_uid &&
                   peer.gid == policy->client_gid
               ? 1
               : 0;
}

static int serve_endpoint(int listener_fd, TokenSignerTee_t *tee,
                          unsigned int idle_timeout_s,
                          const TokenSignerDaemonPolicy_t *policy)
{
    uint64_t deadline_ms = 0u;
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        return -1;
    }
    deadline_ms = ((uint64_t)now.tv_sec * 1000u) +
                  ((uint64_t)now.tv_nsec / 1000000u) +
                  ((uint64_t)idle_timeout_s * 1000u);
    for (;;)
    {
        struct pollfd pfd = {listener_fd, POLLIN, 0};
        uint8_t request[TOKEN_SIGNER_MAX_REQUEST_SIZE] = {0};
        uint8_t response[TOKEN_SIGNER_MAX_RESPONSE_SIZE] = {0};
        uint8_t token[TOKEN_SIGNER_TOKEN_MAX_SIZE] = {0};
        SignerRequest_t parsed = {0};
        size_t response_len = 0u;
        size_t token_len = 0u;
        int poll_rc = poll(&pfd, 1, TOKEN_SIGNER_DAEMON_POLL_TIMEOUT_MS);
        int client_fd;
        ssize_t request_len;
        unsigned int status;

        if (poll_rc < 0 && errno == EINTR)
        {
            continue;
        }
        if (poll_rc < 0)
        {
            return -1;
        }
        if (poll_rc == 0)
        {
            if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
            {
                return -1;
            }
            if (((uint64_t)now.tv_sec * 1000u) +
                    ((uint64_t)now.tv_nsec / 1000000u) >=
                deadline_ms)
            {
                return 0;
            }
            continue;
        }
        client_fd = accept4(listener_fd, NULL, NULL, SOCK_CLOEXEC);
        if (client_fd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        if (!peer_is_authorized(client_fd, policy))
        {
            fprintf(stderr, "token-signer: rejected client credentials\n");
            (void)close(client_fd);
            continue;
        }
        request_len = recv(client_fd, request, sizeof(request), MSG_TRUNC);
        status = request_len > 0 && (size_t)request_len <= sizeof(request)
                     ? (unsigned int)parse_request(
                           request, (size_t)request_len, &parsed)
                      : TOKEN_SIGNER_RESPONSE_STATUS_MALFORMED;
        if (status == TOKEN_SIGNER_RESPONSE_STATUS_OK)
        {
            status = (unsigned int)build_token(tee, &parsed, token,
                                               sizeof(token), &token_len);
        }
        if (status != TOKEN_SIGNER_RESPONSE_STATUS_OK)
        {
            /* Status only; request contents and signature material stay secret. */
            fprintf(stderr, "token-signer: request failed status=%u\n", status);
        }
        if (encode_response(status, parsed.request_id,
                            status == TOKEN_SIGNER_RESPONSE_STATUS_OK ? token
                                                                      : NULL,
                            token_len, response, sizeof(response),
                            &response_len) == 0)
        {
            (void)send(client_fd, response, response_len, MSG_NOSIGNAL);
        }
        (void)close(client_fd);
        if (clock_gettime(CLOCK_MONOTONIC, &now) == 0)
        {
            deadline_ms = ((uint64_t)now.tv_sec * 1000u) +
                          ((uint64_t)now.tv_nsec / 1000000u) +
                          ((uint64_t)idle_timeout_s * 1000u);
        }
    }
}

int token_signer_daemon_run(const char *endpoint, TokenSignerTee_t *tee,
                            unsigned int idle_timeout_s,
                            const TokenSignerDaemonPolicy_t *policy)
{
    struct sockaddr_un address;
    struct stat endpoint_stat;
    int listener_fd = -1;
    int rc = -1;

    if (endpoint == NULL || tee == NULL || idle_timeout_s == 0u || policy == NULL)
    {
        return -1;
    }
    if (strlen(endpoint) >= sizeof(address.sun_path))
    {
        return -1;
    }
    if (lstat(endpoint, &endpoint_stat) == 0)
    {
        if (!S_ISSOCK(endpoint_stat.st_mode) ||
            endpoint_stat.st_uid != policy->signer_uid ||
            endpoint_stat.st_gid != policy->socket_gid ||
            (endpoint_stat.st_mode & 0777u) != TOKEN_SIGNER_DAEMON_SOCKET_MODE ||
            unlink(endpoint) != 0)
        {
            return -1;
        }
    }
    else if (errno != ENOENT)
    {
        return -1;
    }
    listener_fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (listener_fd < 0)
    {
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, endpoint, strlen(endpoint) + 1u);
    if (bind(listener_fd, (const struct sockaddr *)&address,
             sizeof(address)) != 0 ||
        lstat(endpoint, &endpoint_stat) != 0 ||
        endpoint_stat.st_uid != policy->signer_uid ||
        endpoint_stat.st_gid != policy->socket_gid ||
        chmod(endpoint, TOKEN_SIGNER_DAEMON_SOCKET_MODE) != 0 ||
        listen(listener_fd, TOKEN_SIGNER_DAEMON_BACKLOG) != 0)
    {
        (void)close(listener_fd);
        return -1;
    }
    rc = serve_endpoint(listener_fd, tee, idle_timeout_s, policy);
    (void)close(listener_fd);
    (void)unlink(endpoint);
    return rc;
}
