#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "fake_signer.h"
#include "profile.h"
#include "security_access.h"
#include "transport.h"
#include "uds_client.h"

typedef struct
{
    uint8_t last_request[SECURITY_ACCESS_TOKEN_MAX_SIZE + 2u];
    size_t last_request_len;
    unsigned int request_count;
} FakeTransport_t;

static int fake_send(void *ctx, const uint8_t *data, size_t length)
{
    FakeTransport_t *transport = (FakeTransport_t *)ctx;

    assert(length <= sizeof(transport->last_request));
    memcpy(transport->last_request, data, length);
    transport->last_request_len = length;
    transport->request_count++;
    return 0;
}

static int fake_receive(void *ctx, uint8_t *data, size_t data_cap, size_t *data_len,
                        uint32_t timeout_ms)
{
    FakeTransport_t *transport = (FakeTransport_t *)ctx;

    (void)timeout_ms;
    if (transport->last_request[1] == SECURITY_ACCESS_LEVEL_PROGRAMMING_SEED)
    {
        static const uint8_t response[] = {
            SID_SECURITY_ACCESS_POS,
            SECURITY_ACCESS_LEVEL_PROGRAMMING_SEED,
            0x10u,
            0x20u,
            0x30u,
            0x40u,
        };

        assert(data_cap >= sizeof(response));
        memcpy(data, response, sizeof(response));
        *data_len = sizeof(response);
    }
    else
    {
        static const uint8_t response[] = {
            SID_SECURITY_ACCESS_POS,
            SECURITY_ACCESS_LEVEL_PROGRAMMING_KEY,
        };

        assert(transport->last_request_len == 5u);
        assert(transport->last_request[2] == 0xD2u);
        assert(transport->last_request[3] == 0x84u);
        assert(transport->last_request[4] == 0x40u);
        assert(data_cap >= sizeof(response));
        memcpy(data, response, sizeof(response));
        *data_len = sizeof(response);
    }
    return 0;
}

int main(void)
{
    static const TransportOps operations = {fake_send, fake_receive};
    static const uint8_t expected_seed[] = {0x10u, 0x20u, 0x30u, 0x40u};
    FakeTransport_t transport = {{0}, 0u, 0u};
    FakeSignerSession_t signer_server = {0};
    TokenSignerClient_t signer = {0};
    UdsClient client = {0};

    fake_signer_start(&signer_server, expected_seed, sizeof(expected_seed), 0);
    signer.endpoint = signer_server.socket_path;
    signer.expected_peer_uid = geteuid();
    signer.expected_peer_gid = getegid();
    signer.expected_socket_uid = geteuid();
    signer.expected_socket_gid = getegid();
    signer.expected_socket_mode = 0660u;
    signer.timeout_ms = TOKEN_SIGNER_DEFAULT_TIMEOUT_MS;
    uds_client_init(&client, &operations, &transport);

    assert(security_access_unlock(&client, &signer) == 0);
    assert(transport.request_count == 2u);
    return fake_signer_join(&signer_server) == 0 ? 0 : 1;
}