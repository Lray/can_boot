#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "profile.h"
#include "transport.h"
#include "uds_client.h"

#define FAKE_RESPONSE_CAPACITY (UDS_RESPONSE_PENDING_MAX_COUNT + 2u)

typedef struct {
    uint8_t expected_send[300];
    size_t expected_send_len;
    uint8_t responses[FAKE_RESPONSE_CAPACITY][700];
    size_t response_lens[FAKE_RESPONSE_CAPACITY];
    uint32_t response_ready_after_ms[FAKE_RESPONSE_CAPACITY];
    uint32_t recv_timeouts[FAKE_RESPONSE_CAPACITY];
    int send_count;
    int recv_count;
} FakeTransport_t;

static int fake_send(void *ctx, const uint8_t *data, size_t len)
{
    FakeTransport_t *fake = (FakeTransport_t *)ctx;

    assert(len == fake->expected_send_len);
    assert(memcmp(data, fake->expected_send, len) == 0);
    fake->send_count++;
    return 0;
}

static int fake_recv(void *ctx, uint8_t *data, size_t data_cap,
                     size_t *data_len, uint32_t timeout_ms)
{
    FakeTransport_t *fake = (FakeTransport_t *)ctx;
    int index = fake->recv_count;
    size_t copy_len = 0u;

    assert(index < (int)FAKE_RESPONSE_CAPACITY);
    fake->recv_timeouts[index] = timeout_ms;
    if (timeout_ms < fake->response_ready_after_ms[index])
    {
        return -1;
    }
    copy_len = fake->response_lens[index];
    if (copy_len > data_cap)
    {
        copy_len = data_cap;
    }
    memcpy(data, fake->responses[index], copy_len);
    *data_len = fake->response_lens[index];
    fake->recv_count++;
    return 0;
}

static UdsClient make_client(FakeTransport_t *fake)
{
    static const TransportOps ops = {
        fake_send,
        fake_recv,
    };
    UdsClient client;

    uds_client_init(&client, &ops, fake);
    return client;
}

static void set_session_response(FakeTransport_t *fake, int index, uint8_t session,
                                 uint16_t p2_server_max_ms,
                                 uint16_t p2_star_server_max_wire)
{
    assert(index >= 0 && index < (int)FAKE_RESPONSE_CAPACITY);
    fake->responses[index][0] = SID_DIAGNOSTIC_SESSION_CONTROL_POS;
    fake->responses[index][1] = session;
    fake->responses[index][2] = (uint8_t)(p2_server_max_ms >> 8);
    fake->responses[index][3] = (uint8_t)p2_server_max_ms;
    fake->responses[index][4] = (uint8_t)(p2_star_server_max_wire >> 8);
    fake->responses[index][5] = (uint8_t)p2_star_server_max_wire;
    fake->response_lens[index] = UDS_SESSION_CONTROL_RESPONSE_LENGTH;
}

static void test_enter_programming_session(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);

    fake.expected_send[0] = SID_DIAGNOSTIC_SESSION_CONTROL;
    fake.expected_send[1] = SESSION_PROGRAMMING;
    fake.expected_send_len = 2;
    set_session_response(&fake, 0, SESSION_PROGRAMMING, P2_SERVER_DEFAULT_MS,
                         P2_STAR_SERVER_DEFAULT_WIRE);

    assert(uds_enter_session(&client, SESSION_PROGRAMMING) == 0);
    assert(fake.send_count == 1);
    assert(fake.recv_count == 1);
    assert(fake.recv_timeouts[0] == P2_SERVER_DEFAULT_MS);
    assert(client.p2_server_max_ms == P2_SERVER_DEFAULT_MS);
    assert(client.p2_star_server_max_ms == P2_STAR_SERVER_DEFAULT_MS);
}

static void test_session_response_negotiates_timing(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t data[8] = {0};
    size_t data_len = 0u;

    fake.expected_send[0] = SID_DIAGNOSTIC_SESSION_CONTROL;
    fake.expected_send[1] = SESSION_EXTENDED;
    fake.expected_send_len = 2u;
    set_session_response(&fake, 0, SESSION_EXTENDED, 20u, 100u);

    assert(uds_enter_session(&client, SESSION_EXTENDED) == 0);
    assert(client.p2_server_max_ms == 20u);
    assert(client.p2_star_server_max_ms == 1000u);

    fake.expected_send[0] = SID_READ_DATA_BY_IDENTIFIER;
    fake.expected_send[1] = 0xF1u;
    fake.expected_send[2] = 0x81u;
    fake.expected_send_len = 3u;
    fake.responses[1][0] = NEGATIVE_RESPONSE_SID;
    fake.responses[1][1] = SID_READ_DATA_BY_IDENTIFIER;
    fake.responses[1][2] = NRC_RESPONSE_PENDING;
    fake.response_lens[1] = UDS_NEGATIVE_RESPONSE_LENGTH;
    fake.responses[2][0] = SID_READ_DATA_BY_IDENTIFIER_POS;
    fake.responses[2][1] = 0xF1u;
    fake.responses[2][2] = 0x81u;
    fake.responses[2][3] = 0x56u;
    fake.response_lens[2] = 4u;

    assert(uds_read_did(&client, DID_APP_VERSION, data, sizeof(data), &data_len) == 0);
    assert(fake.recv_timeouts[0] == P2_SERVER_DEFAULT_MS);
    assert(fake.recv_timeouts[1] == 20u);
    assert(fake.recv_timeouts[2] == 1000u);
    assert(data_len == 1u);
    assert(data[0] == 0x56u);
}

static void test_session_response_requires_timing_parameters(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);

    fake.expected_send[0] = SID_DIAGNOSTIC_SESSION_CONTROL;
    fake.expected_send[1] = SESSION_PROGRAMMING;
    fake.expected_send_len = 2u;
    fake.responses[0][0] = SID_DIAGNOSTIC_SESSION_CONTROL_POS;
    fake.responses[0][1] = SESSION_PROGRAMMING;
    fake.response_lens[0] = 2u;

    assert(uds_enter_session(&client, SESSION_PROGRAMMING) == UDS_ERR_MALFORMED_RESPONSE);
    assert(client.p2_server_max_ms == P2_SERVER_DEFAULT_MS);
    assert(client.p2_star_server_max_ms == P2_STAR_SERVER_DEFAULT_MS);
}

static void test_tester_present(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);

    fake.expected_send[0] = SID_TESTER_PRESENT;
    fake.expected_send[1] = 0x00;
    fake.expected_send_len = 2;
    fake.responses[0][0] = SID_TESTER_PRESENT_POS;
    fake.responses[0][1] = 0x00;
    fake.response_lens[0] = 2;

    assert(uds_tester_present(&client) == 0);
}

static void test_ecu_reset_restores_default_timing(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);

    client.p2_server_max_ms = 20u;
    client.p2_star_server_max_ms = 1000u;
    fake.expected_send[0] = SID_ECU_RESET;
    fake.expected_send[1] = SUB_HARD_RESET;
    fake.expected_send_len = 2u;
    fake.responses[0][0] = SID_ECU_RESET_POS;
    fake.responses[0][1] = SUB_HARD_RESET;
    fake.response_lens[0] = 2u;

    assert(uds_ecu_reset_hard(&client) == 0);
    assert(fake.recv_timeouts[0] == 20u);
    assert(client.p2_server_max_ms == P2_SERVER_DEFAULT_MS);
    assert(client.p2_star_server_max_ms == P2_STAR_SERVER_DEFAULT_MS);
}

static void test_security_token_requires_response_pending_for_extended_processing(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t token[292];
    size_t index;

    for (index = 0u; index < sizeof(token); index++)
    {
        token[index] = (uint8_t)(index ^ 0xA5u);
    }
    fake.expected_send[0] = SID_SECURITY_ACCESS;
    fake.expected_send[1] = SECURITY_ACCESS_LEVEL_PROGRAMMING_KEY;
    memcpy(fake.expected_send + 2u, token, sizeof(token));
    fake.expected_send_len = sizeof(token) + 2u;
    fake.responses[0][0] = NEGATIVE_RESPONSE_SID;
    fake.responses[0][1] = SID_SECURITY_ACCESS;
    fake.responses[0][2] = NRC_RESPONSE_PENDING;
    fake.response_lens[0] = UDS_NEGATIVE_RESPONSE_LENGTH;
    fake.responses[1][0] = SID_SECURITY_ACCESS_POS;
    fake.responses[1][1] = SECURITY_ACCESS_LEVEL_PROGRAMMING_KEY;
    fake.response_lens[1] = 2u;

    assert(uds_security_send_token(&client, token, sizeof(token)) == 0);
    assert(fake.send_count == 1);
    assert(fake.recv_count == 2);
    assert(fake.recv_timeouts[0] > P2_SERVER_DEFAULT_MS);
    assert(fake.recv_timeouts[0] < P2_STAR_SERVER_DEFAULT_MS);
    assert(fake.recv_timeouts[1] == P2_STAR_SERVER_DEFAULT_MS);
}

static void test_read_did_copies_payload(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t data[8];
    size_t data_len = 0;

    fake.expected_send[0] = SID_READ_DATA_BY_IDENTIFIER;
    fake.expected_send[1] = 0xF1;
    fake.expected_send[2] = 0x80;
    fake.expected_send_len = 3;
    fake.responses[0][0] = SID_READ_DATA_BY_IDENTIFIER_POS;
    fake.responses[0][1] = 0xF1;
    fake.responses[0][2] = 0x80;
    fake.responses[0][3] = 0x12;
    fake.responses[0][4] = 0x34;
    fake.response_lens[0] = 5;

    assert(uds_read_did(&client, DID_BOOT_VERSION, data, sizeof(data), &data_len) == 0);
    assert(data_len == 2);
    assert(data[0] == 0x12);
    assert(data[1] == 0x34);
}

static void test_negative_response_is_reported(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t data[8] = {0};
    size_t data_len = 0;
    fake.expected_send[0] = SID_READ_DATA_BY_IDENTIFIER;
    fake.expected_send[1] = 0xFF;
    fake.expected_send[2] = 0xFF;
    fake.expected_send_len = 3;
    fake.responses[0][0] = NEGATIVE_RESPONSE_SID;
    fake.responses[0][1] = SID_READ_DATA_BY_IDENTIFIER;
    fake.responses[0][2] = NRC_REQUEST_OUT_OF_RANGE;
    fake.response_lens[0] = 3;

    assert(uds_read_did(&client, 0xFFFFu, data, sizeof(data), &data_len) ==
           UDS_ERR_NEGATIVE_RESPONSE);
    assert(client.last_nrc == NRC_REQUEST_OUT_OF_RANGE);
}

static void test_busy_repeat_request_is_reported(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t data[8] = {0};
    size_t data_len = 0u;

    fake.expected_send[0] = SID_READ_DATA_BY_IDENTIFIER;
    fake.expected_send[1] = 0xF1u;
    fake.expected_send[2] = 0x81u;
    fake.expected_send_len = 3u;
    fake.responses[0][0] = NEGATIVE_RESPONSE_SID;
    fake.responses[0][1] = SID_READ_DATA_BY_IDENTIFIER;
    fake.responses[0][2] = NRC_BUSY_REPEAT_REQUEST;
    fake.response_lens[0] = UDS_NEGATIVE_RESPONSE_LENGTH;

    assert(uds_read_did(&client, DID_APP_VERSION, data, sizeof(data), &data_len) ==
           UDS_ERR_NEGATIVE_RESPONSE);
    assert(client.last_nrc == NRC_BUSY_REPEAT_REQUEST);
}

static void test_malformed_negative_response_is_rejected(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t data[8] = {0};
    size_t data_len = 0u;

    fake.expected_send[0] = SID_READ_DATA_BY_IDENTIFIER;
    fake.expected_send[1] = 0xF1u;
    fake.expected_send[2] = 0x81u;
    fake.expected_send_len = 3u;
    fake.responses[0][0] = NEGATIVE_RESPONSE_SID;
    fake.responses[0][1] = SID_READ_DATA_BY_IDENTIFIER;
    fake.response_lens[0] = 2u;

    assert(uds_read_did(&client, DID_APP_VERSION, data, sizeof(data), &data_len) ==
           UDS_ERR_MALFORMED_RESPONSE);
    assert(client.last_nrc == 0u);
}

static void test_response_pending_switches_to_p2_star(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t data[8];
    size_t data_len = 0;

    fake.expected_send[0] = SID_READ_DATA_BY_IDENTIFIER;
    fake.expected_send[1] = 0xF1;
    fake.expected_send[2] = 0x81;
    fake.expected_send_len = 3;
    fake.responses[0][0] = NEGATIVE_RESPONSE_SID;
    fake.responses[0][1] = SID_READ_DATA_BY_IDENTIFIER;
    fake.responses[0][2] = NRC_RESPONSE_PENDING;
    fake.response_lens[0] = 3;
    fake.responses[1][0] = SID_READ_DATA_BY_IDENTIFIER_POS;
    fake.responses[1][1] = 0xF1;
    fake.responses[1][2] = 0x81;
    fake.responses[1][3] = 0x56;
    fake.response_lens[1] = 4;

    assert(uds_read_did(&client, DID_APP_VERSION, data, sizeof(data), &data_len) == 0);
    assert(fake.send_count == 1);
    assert(fake.recv_count == 2);
    assert(fake.recv_timeouts[0] == P2_SERVER_DEFAULT_MS);
    assert(fake.recv_timeouts[1] == P2_STAR_SERVER_DEFAULT_MS);
    assert(data_len == 1);
    assert(data[0] == 0x56);
    assert(client.last_nrc == 0u);
}

static void test_response_pending_then_timeout_has_no_terminal_nrc(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t data[8] = {0};
    size_t data_len = 0u;
    fake.expected_send[0] = SID_READ_DATA_BY_IDENTIFIER;
    fake.expected_send[1] = 0xF1u;
    fake.expected_send[2] = 0x81u;
    fake.expected_send_len = 3u;
    fake.responses[0][0] = NEGATIVE_RESPONSE_SID;
    fake.responses[0][1] = SID_READ_DATA_BY_IDENTIFIER;
    fake.responses[0][2] = NRC_RESPONSE_PENDING;
    fake.response_lens[0] = 3u;
    fake.response_ready_after_ms[1] = P2_STAR_SERVER_DEFAULT_MS + 1u;

    assert(uds_read_did(&client, DID_APP_VERSION, data, sizeof(data), &data_len) ==
           UDS_ERR_TIMEOUT);
    assert(fake.send_count == 1);
    assert(fake.recv_count == 1);
    assert(fake.recv_timeouts[0] == P2_SERVER_DEFAULT_MS);
    assert(fake.recv_timeouts[1] == P2_STAR_SERVER_DEFAULT_MS);
    assert(data_len == 0u);
    assert(client.last_nrc == 0u);
}

static void test_response_pending_limit_accepts_ecu_budget(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t data[8] = {0};
    size_t data_len = 0u;
    uint8_t index;

    fake.expected_send[0] = SID_READ_DATA_BY_IDENTIFIER;
    fake.expected_send[1] = 0xF1u;
    fake.expected_send[2] = 0x81u;
    fake.expected_send_len = 3u;
    for (index = 0u; index < UDS_RESPONSE_PENDING_MAX_COUNT; ++index)
    {
        fake.responses[index][0] = NEGATIVE_RESPONSE_SID;
        fake.responses[index][1] = SID_READ_DATA_BY_IDENTIFIER;
        fake.responses[index][2] = NRC_RESPONSE_PENDING;
        fake.response_lens[index] = UDS_NEGATIVE_RESPONSE_LENGTH;
    }
    fake.responses[UDS_RESPONSE_PENDING_MAX_COUNT][0] = SID_READ_DATA_BY_IDENTIFIER_POS;
    fake.responses[UDS_RESPONSE_PENDING_MAX_COUNT][1] = 0xF1u;
    fake.responses[UDS_RESPONSE_PENDING_MAX_COUNT][2] = 0x81u;
    fake.responses[UDS_RESPONSE_PENDING_MAX_COUNT][3] = 0x01u;
    fake.response_lens[UDS_RESPONSE_PENDING_MAX_COUNT] = 4u;

    assert(uds_read_did(&client, DID_APP_VERSION, data, sizeof(data), &data_len) == 0);
    assert(fake.recv_count == (int)(UDS_RESPONSE_PENDING_MAX_COUNT + 1u));
    assert(fake.recv_timeouts[0] == P2_SERVER_DEFAULT_MS);
    for (index = 1u; index <= UDS_RESPONSE_PENDING_MAX_COUNT; ++index)
    {
        assert(fake.recv_timeouts[index] == P2_STAR_SERVER_DEFAULT_MS);
    }
}

static void test_response_pending_limit_rejects_extra_pending(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t data[8] = {0};
    size_t data_len = 0u;
    uint8_t index;

    fake.expected_send[0] = SID_READ_DATA_BY_IDENTIFIER;
    fake.expected_send[1] = 0xF1u;
    fake.expected_send[2] = 0x81u;
    fake.expected_send_len = 3u;
    for (index = 0u; index <= UDS_RESPONSE_PENDING_MAX_COUNT; ++index)
    {
        fake.responses[index][0] = NEGATIVE_RESPONSE_SID;
        fake.responses[index][1] = SID_READ_DATA_BY_IDENTIFIER;
        fake.responses[index][2] = NRC_RESPONSE_PENDING;
        fake.response_lens[index] = UDS_NEGATIVE_RESPONSE_LENGTH;
    }

    assert(uds_read_did(&client, DID_APP_VERSION, data, sizeof(data), &data_len) ==
           UDS_ERR_RESPONSE_PENDING_LIMIT);
    assert(fake.recv_count == (int)(UDS_RESPONSE_PENDING_MAX_COUNT + 1u));
}

static void test_read_did_handles_large_response(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t data[497] = {0};
    size_t data_len = 0u;
    size_t i = 0u;

    fake.expected_send[0] = SID_READ_DATA_BY_IDENTIFIER;
    fake.expected_send[1] = 0xF1u;
    fake.expected_send[2] = 0x81u;
    fake.expected_send_len = 3u;
    fake.responses[0][0] = SID_READ_DATA_BY_IDENTIFIER_POS;
    fake.responses[0][1] = 0xF1u;
    fake.responses[0][2] = 0x81u;
    for (i = 3u; i < 500u; ++i)
    {
        fake.responses[0][i] = (uint8_t)i;
    }
    fake.response_lens[0] = 500u;

    assert(uds_read_did(&client, DID_APP_VERSION, data, sizeof(data), &data_len) == 0);
    assert(data_len == 497u);
    assert(memcmp(data, fake.responses[0] + 3u, 497u) == 0);
}

static void test_read_did_reports_transport_truncation(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t data[8] = {0};
    size_t data_len = 0u;
    size_t i = 0u;
    fake.expected_send[0] = SID_READ_DATA_BY_IDENTIFIER;
    fake.expected_send[1] = 0xF1u;
    fake.expected_send[2] = 0x81u;
    fake.expected_send_len = 3u;
    fake.responses[0][0] = SID_READ_DATA_BY_IDENTIFIER_POS;
    fake.responses[0][1] = 0xF1u;
    fake.responses[0][2] = 0x81u;
    for (i = 3u; i < 600u; ++i)
    {
        fake.responses[0][i] = (uint8_t)i;
    }
    fake.response_lens[0] = 600u;

    assert(uds_read_did(&client, DID_APP_VERSION, data, sizeof(data), &data_len) ==
           UDS_ERR_BUFFER_TOO_SMALL);
    assert(client.last_nrc == 0u);
}

static void test_read_did_rejects_mismatched_did(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t data[8] = {0};
    size_t data_len = 0u;
    fake.expected_send[0] = SID_READ_DATA_BY_IDENTIFIER;
    fake.expected_send[1] = 0xF1u;
    fake.expected_send[2] = 0x81u;
    fake.expected_send_len = 3u;
    fake.responses[0][0] = SID_READ_DATA_BY_IDENTIFIER_POS;
    fake.responses[0][1] = 0xF1u;
    fake.responses[0][2] = 0x82u;
    fake.responses[0][3] = 0x01u;
    fake.response_lens[0] = 4u;

    assert(uds_read_did(&client, DID_APP_VERSION, data, sizeof(data), &data_len) ==
           UDS_ERR_UNEXPECTED_RESPONSE);
    assert(client.last_nrc == 0u);
}

static void test_transfer_data_uses_extended_timeout_for_full_block(void)
{
    FakeTransport_t fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t block[TRANSFER_BLOCK_PAYLOAD];
    size_t i;

    for (i = 0; i < sizeof(block); ++i) {
        block[i] = (uint8_t)i;
    }

    fake.expected_send[0] = SID_TRANSFER_DATA;
    fake.expected_send[1] = 0x01;
    memcpy(fake.expected_send + 2, block, sizeof(block));
    fake.expected_send_len = sizeof(block) + 2u;
    fake.responses[0][0] = SID_TRANSFER_DATA_POS;
    fake.responses[0][1] = 0x01;
    fake.response_lens[0] = 2;
    fake.response_ready_after_ms[0] = 80u;

    assert(uds_transfer_data(&client, 0x01u, block, sizeof(block)) == 0);
    assert(fake.send_count == 1);
assert(fake.recv_count == 1);
    assert(fake.recv_timeouts[0] >= fake.response_ready_after_ms[0]);
}

int main(void)
{
    test_enter_programming_session();
    test_session_response_negotiates_timing();
    test_session_response_requires_timing_parameters();
    test_tester_present();
    test_ecu_reset_restores_default_timing();
    test_security_token_requires_response_pending_for_extended_processing();
    test_read_did_copies_payload();
    test_negative_response_is_reported();
    test_busy_repeat_request_is_reported();
    test_malformed_negative_response_is_rejected();
    test_response_pending_switches_to_p2_star();
    test_response_pending_then_timeout_has_no_terminal_nrc();
    test_response_pending_limit_accepts_ecu_budget();
    test_response_pending_limit_rejects_extra_pending();
    test_read_did_handles_large_response();
    test_read_did_reports_transport_truncation();
    test_read_did_rejects_mismatched_did();
    test_transfer_data_uses_extended_timeout_for_full_block();
    return 0;
}
