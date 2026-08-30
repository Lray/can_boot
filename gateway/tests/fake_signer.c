#include "fake_signer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "qcbor/qcbor_spiffy_decode.h"
#include "token_signer_protocol.h"

static const uint8_t fake_token[] = {0xD2u, 0x84u, 0x40u};

static void assert_request_contract(const uint8_t *packet, size_t packet_len,
                                    uint8_t request_id_out[16],
                                    const uint8_t *expected_seed,
                                    size_t expected_seed_len)
{
    QCBORDecodeContext context;
    QCBORItem array;
    UsefulBufC request_id = NULLUsefulBufC;
    UsefulBufC seed = NULLUsefulBufC;
    uint64_t seed_len = 0u;

    QCBORDecode_Init(&context, (UsefulBufC){packet, packet_len}, QCBOR_DECODE_MODE_NORMAL);
    QCBORDecode_EnterArray(&context, &array);
    QCBORDecode_GetByteString(&context, &request_id);
    QCBORDecode_GetByteString(&context, &seed);
    QCBORDecode_GetUInt64(&context, &seed_len);
    QCBORDecode_ExitArray(&context);
    assert(QCBORDecode_Finish(&context) == QCBOR_SUCCESS);
    assert(array.val.uCount == 3u);
    assert(request_id.len == 16u);
    assert(seed.len == expected_seed_len);
    assert(seed_len == seed.len);
    assert(memcmp(seed.ptr, expected_seed, seed.len) == 0);
    memcpy(request_id_out, request_id.ptr, 16u);
}

static void *fake_signer_thread(void *argument)
{
    FakeSignerSession_t *session = (FakeSignerSession_t *)argument;
    uint8_t request[TOKEN_SIGNER_MAX_REQUEST_SIZE];
    uint8_t response[1u + 1u + 1u + 16u + 1u + sizeof(fake_token)];
    ssize_t request_len;
    int client_fd = accept4(session->listen_fd, NULL, NULL, SOCK_CLOEXEC);
    size_t offset = 0u;
    uint8_t request_id[16];

    assert(client_fd >= 0);
    request_len = recv(client_fd, request, sizeof(request), MSG_TRUNC);
    assert(request_len > 0 && request_len <= (ssize_t)sizeof(request));
    assert_request_contract(request, (size_t)request_len, request_id,
                            session->expected_seed, session->expected_seed_len);

    response[offset++] = 0x83u;
    response[offset++] = 0x00u;
    response[offset++] = 0x50u;
    memcpy(response + offset, request_id, sizeof(request_id));
    if (session->mismatch_request_id)
    {
        response[offset] ^= 0x80u;
    }
    offset += 16u;
    response[offset++] = (uint8_t)(0x40u | sizeof(fake_token));
    memcpy(response + offset, fake_token, sizeof(fake_token));
    offset += sizeof(fake_token);
    assert(send(client_fd, response, offset, MSG_NOSIGNAL) == (ssize_t)offset);
    assert(close(client_fd) == 0);
    return NULL;
}

void fake_signer_start(FakeSignerSession_t *session, const uint8_t *expected_seed,
                       size_t expected_seed_len, int mismatch_request_id)
{
    struct sockaddr_un address;
    int length;

    memset(session, 0, sizeof(*session));
    strcpy(session->directory, "/tmp/gateway-fake-signer-XXXXXX");
    assert(mkdtemp(session->directory) != NULL);
    length = snprintf(session->socket_path, 108u, "%s/v1.sock", session->directory);
    assert(length > 0 && length < 108);
    session->listen_fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    assert(session->listen_fd >= 0);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    assert(strlen(session->socket_path) < sizeof(address.sun_path));
    memcpy(address.sun_path, session->socket_path, strlen(session->socket_path) + 1u);
    assert(bind(session->listen_fd, (const struct sockaddr *)&address, sizeof(address)) == 0);
    assert(chmod(session->socket_path, 0660u) == 0);
    assert(listen(session->listen_fd, 1) == 0);

    session->expected_seed = expected_seed;
    session->expected_seed_len = expected_seed_len;
    session->mismatch_request_id = mismatch_request_id;
    assert(pthread_create(&session->thread, NULL, fake_signer_thread, session) == 0);
}

int fake_signer_join(FakeSignerSession_t *session)
{
    void *thread_result = NULL;

    assert(pthread_join(session->thread, &thread_result) == 0);
    assert(close(session->listen_fd) == 0);
    assert(unlink(session->socket_path) == 0);
    assert(rmdir(session->directory) == 0);
    return thread_result == NULL ? 0 : -1;
}