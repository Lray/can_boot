#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "profile.h"
#include "security_access.h"
#include "transport.h"
#include "uds_client.h"

typedef struct
{
    uint8_t last_request[SECURITY_ACCESS_TOKEN_MAX_SIZE + 2u];
    size_t last_request_len;
    unsigned int request_count;
    bool zero_seed;
    bool response_available;
} FakeTransport_t;

static const uint8_t expected_seed[] = {0x10u, 0x20u, 0x30u, 0x40u};

int security_token_tee_issue(SecurityTokenTee_t *tee, const uint8_t seed[4],
                             uint8_t *token_out, size_t token_cap,
                             size_t *token_len_out)
{
    static const uint8_t token[] = {0xD2u, 0x84u, 0x40u};

    assert(tee != NULL);
    assert(memcmp(seed, expected_seed, sizeof(expected_seed)) == 0);
    assert(token_cap >= sizeof(token));
    memcpy(token_out, token, sizeof(token));
    *token_len_out = sizeof(token);
    return 0;
}

static int fake_send(void *ctx, const uint8_t *data, size_t length)
{
    FakeTransport_t *transport = (FakeTransport_t *)ctx;

    assert(length <= sizeof(transport->last_request));
    memcpy(transport->last_request, data, length);
    transport->last_request_len = length;
    transport->request_count++;
    transport->response_available = true;
    return 0;
}

static int fake_receive(void *ctx, uint8_t *data, size_t data_cap, size_t *data_len,
                        uint32_t timeout_ms)
{
    FakeTransport_t *transport = (FakeTransport_t *)ctx;

    (void)timeout_ms;
    if (!transport->response_available)
    {
        return -1;
    }
    transport->response_available = false;
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
        if (transport->zero_seed)
        {
            memset(data + 2u, 0, SECURITY_ACCESS_SEED_SIZE);
        }
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
    FakeTransport_t transport = {0};
    SecurityTokenTee_t *token_tee = (SecurityTokenTee_t *)&transport;
    UdsClient client = {0};

    uds_client_init(&client, &operations, &transport);

    assert(security_access_unlock(&client, token_tee) == 0);
    assert(transport.request_count == 2u);

    transport = (FakeTransport_t){.zero_seed = true};
    uds_client_init(&client, &operations, &transport);
    assert(security_access_unlock(&client, token_tee) == 0);
    assert(transport.request_count == 1u);
    return 0;
}
