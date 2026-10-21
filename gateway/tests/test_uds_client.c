#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "profile.h"
#include "transport.h"
#include "uds_client.h"
#include "util.h"

#define FAKE_CAPACITY 16U

typedef struct
{
    uint8_t expected_send[FAKE_CAPACITY][UDS_TP_MTU];
    size_t expected_send_len[FAKE_CAPACITY];
    uint8_t response[FAKE_CAPACITY][UDS_TP_MTU];
    size_t response_len[FAKE_CAPACITY];
    bool send_failure[FAKE_CAPACITY];
    bool response_available;
    size_t send_count;
    size_t recv_count;
} FakeTransport;

static int fake_send(void *ctx, const uint8_t *data, size_t len)
{
    FakeTransport *fake = ctx;
    size_t index = fake->send_count++;

    assert(index < FAKE_CAPACITY);
    assert(len == fake->expected_send_len[index]);
    assert(memcmp(data, fake->expected_send[index], len) == 0);
    fake->response_available = !fake->send_failure[index];
    return fake->send_failure[index] ? -1 : 0;
}

static int fake_recv(void *ctx, uint8_t *data, size_t capacity,
                     size_t *length, uint32_t timeout_ms)
{
    FakeTransport *fake = ctx;
    size_t index;

    (void)timeout_ms;
    if (!fake->response_available)
    {
        return -1;
    }
    index = fake->recv_count++;
    assert(index < FAKE_CAPACITY);
    assert(fake->response_len[index] <= capacity);
    memcpy(data, fake->response[index], fake->response_len[index]);
    *length = fake->response_len[index];
    fake->response_available =
        fake->response_len[index] == 3U &&
        fake->response[index][0] == 0x7FU &&
        fake->response[index][2] == 0x78U;
    return 0;
}

static void make_client(FakeTransport *fake, UdsClient *client)
{
    static const TransportOps operations = {fake_send, fake_recv};

    uds_client_init(client, &operations, fake);
}

static void expect_request(FakeTransport *fake, size_t index,
                           const uint8_t *request, size_t request_len,
                           const uint8_t *response, size_t response_len)
{
    memcpy(fake->expected_send[index], request, request_len);
    fake->expected_send_len[index] = request_len;
    memcpy(fake->response[index], response, response_len);
    fake->response_len[index] = response_len;
}

static void test_session_uses_upstream_timing_negotiation(void)
{
    FakeTransport fake = {0};
    UdsClient client;
    static const uint8_t request[] = {0x10U, 0x02U};
    static const uint8_t response[] = {
        0x50U, 0x02U, 0x00U, 0x14U, 0x00U, 0x64U,
    };

    make_client(&fake, &client);
    expect_request(&fake, 0U, request, sizeof(request),
                   response, sizeof(response));
    assert(uds_enter_session(&client, SESSION_PROGRAMMING) == UDS_OK);
    assert(client.upstream.p2_ms == 20U);
    assert(client.upstream.p2_star_ms == 1000U);
}

static void test_tester_present_and_security_access(void)
{
    FakeTransport fake = {0};
    UdsClient client;
    uint8_t seed[SECURITY_ACCESS_SEED_SIZE] = {0};
    size_t seed_len = 0U;
    static const uint8_t tester_request[] = {0x3EU, 0x00U};
    static const uint8_t tester_response[] = {0x7EU, 0x00U};
    static const uint8_t seed_request[] = {0x27U, 0x01U};
    static const uint8_t seed_response[] = {
        0x67U, 0x01U, 0x11U, 0x22U, 0x33U, 0x44U,
    };
    static const uint8_t token_request[] = {0x27U, 0x02U, 0xA5U};
    static const uint8_t token_response[] = {0x67U, 0x02U};

    make_client(&fake, &client);
    expect_request(&fake, 0U, tester_request, sizeof(tester_request),
                   tester_response, sizeof(tester_response));
    expect_request(&fake, 1U, seed_request, sizeof(seed_request),
                   seed_response, sizeof(seed_response));
    expect_request(&fake, 2U, token_request, sizeof(token_request),
                   token_response, sizeof(token_response));

    assert(uds_tester_present(&client) == UDS_OK);
    assert(uds_security_request_seed(&client, seed, sizeof(seed), &seed_len) ==
           UDS_OK);
    assert(seed_len == sizeof(seed));
    assert(memcmp(seed, &seed_response[2], sizeof(seed)) == 0);
    assert(uds_security_send_token(&client, &token_request[2], 1U) == UDS_OK);
}

static void test_read_did_and_exact_nrc(void)
{
    FakeTransport fake = {0};
    UdsClient client;
    uint8_t data[4] = {0};
    size_t data_len = 0U;
    static const uint8_t did_request[] = {0x22U, 0xF1U, 0xF1U};
    static const uint8_t did_response[] = {
        0x62U, 0xF1U, 0xF1U, 0x12U, 0x34U,
    };
    static const uint8_t missing_request[] = {0x22U, 0xFFU, 0xFFU};
    static const uint8_t missing_response[] = {0x7FU, 0x22U, 0x31U};

    make_client(&fake, &client);
    expect_request(&fake, 0U, did_request, sizeof(did_request),
                   did_response, sizeof(did_response));
    expect_request(&fake, 1U, missing_request, sizeof(missing_request),
                   missing_response, sizeof(missing_response));

    assert(uds_read_did(&client, DID_APP_VERSION, data, sizeof(data),
                        &data_len) == UDS_OK);
    assert(data_len == 2U && data[0] == 0x12U && data[1] == 0x34U);
    assert(uds_read_did(&client, 0xFFFFU, data, sizeof(data), &data_len) ==
           UDS_NRC_RequestOutOfRange);
    assert(client.last_nrc == UDS_NRC_RequestOutOfRange);
}

static void test_read_did_rejects_mismatched_identifier(void)
{
    FakeTransport fake = {0};
    UdsClient client;
    uint8_t data[1] = {0};
    size_t data_len = 0U;
    static const uint8_t request[] = {0x22U, 0xF1U, 0xF1U};
    static const uint8_t response[] = {0x62U, 0xF1U, 0xF2U, 0x01U};

    make_client(&fake, &client);
    expect_request(&fake, 0U, request, sizeof(request),
                   response, sizeof(response));
    assert(uds_read_did(&client, DID_APP_VERSION, data, sizeof(data),
                        &data_len) == UDS_ERR_DID_MISMATCH);
}

static void test_response_pending_has_no_project_count_limit(void)
{
    FakeTransport fake = {0};
    UdsClient client;
    uint8_t data[1] = {0};
    size_t data_len = 0U;
    static const uint8_t request[] = {0x22U, 0xF1U, 0xF1U};
    static const uint8_t pending[] = {0x7FU, 0x22U, 0x78U};
    static const uint8_t positive[] = {0x62U, 0xF1U, 0xF1U, 0x5AU};

    make_client(&fake, &client);
    memcpy(fake.expected_send[0], request, sizeof(request));
    fake.expected_send_len[0] = sizeof(request);
    for (size_t index = 0U; index < 10U; ++index)
    {
        memcpy(fake.response[index], pending, sizeof(pending));
        fake.response_len[index] = sizeof(pending);
    }
    memcpy(fake.response[10], positive, sizeof(positive));
    fake.response_len[10] = sizeof(positive);

    assert(uds_read_did(&client, DID_APP_VERSION, data, sizeof(data),
                        &data_len) == UDS_OK);
    assert(fake.recv_count == 11U);
    assert(data_len == 1U && data[0] == 0x5AU);
}

static void test_download_transfer_and_exit(void)
{
    FakeTransport fake = {0};
    UdsClient client;
    uint16_t block_length = 0U;
    static const uint8_t download_request[] = {
        0x34U, 0x00U, 0x44U, 0, 0, 0, 0, 0, 0, 0, 2,
    };
    static const uint8_t download_response[] = {0x74U, 0x20U, 0x01U, 0x02U};
    static const uint8_t transfer_request[] = {0x36U, 0x01U, 0xAAU, 0xBBU};
    static const uint8_t transfer_response[] = {0x76U, 0x01U};
    static const uint8_t exit_request[] = {0x37U};
    static const uint8_t exit_response[] = {0x77U};

    make_client(&fake, &client);
    expect_request(&fake, 0U, download_request, sizeof(download_request),
                   download_response, sizeof(download_response));
    expect_request(&fake, 1U, transfer_request, sizeof(transfer_request),
                   transfer_response, sizeof(transfer_response));
    expect_request(&fake, 2U, exit_request, sizeof(exit_request),
                   exit_response, sizeof(exit_response));

    assert(uds_request_download(&client, 2U, &block_length) == UDS_OK);
    assert(block_length == 258U);
    assert(uds_transfer_data(&client, 1U, &transfer_request[2], 2U) == UDS_OK);
    assert(uds_request_transfer_exit(&client) == UDS_OK);
    assert(client.max_block_length == 0U);
}

static void test_transport_error_and_expired_deadline(void)
{
    FakeTransport fake = {0};
    UdsClient client;
    static const uint8_t request[] = {0x3EU, 0x00U};

    make_client(&fake, &client);
    memcpy(fake.expected_send[0], request, sizeof(request));
    fake.expected_send_len[0] = sizeof(request);
    fake.send_failure[0] = true;
    assert(uds_tester_present(&client) == UDS_ERR_TPORT);

    memset(&fake, 0, sizeof(fake));
    make_client(&fake, &client);
    uds_client_set_operation_deadline(&client, util_monotonic_ms());
    assert(uds_tester_present(&client) == UDS_ERR_TIMEOUT);
    assert(fake.send_count == 0U && fake.recv_count == 0U);
}

int main(void)
{
    test_session_uses_upstream_timing_negotiation();
    test_tester_present_and_security_access();
    test_read_did_and_exact_nrc();
    test_read_did_rejects_mismatched_identifier();
    test_response_pending_has_no_project_count_limit();
    test_download_transfer_and_exit();
    test_transport_error_and_expired_deadline();
    return 0;
}
