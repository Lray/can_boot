#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "profile.h"
#include "resume_transfer.h"
#include "sha256.h"
#include "transport.h"
#include "uds_client.h"

#define TEST_IMAGE_SIZE (2u * META_CHECKPOINT_INTERVAL)
#define MAX_EXPECTED_TRANSACTIONS 80u

typedef struct
{
    uint8_t expected_send[MAX_EXPECTED_TRANSACTIONS][300];
    size_t expected_send_len[MAX_EXPECTED_TRANSACTIONS];
    uint8_t responses[MAX_EXPECTED_TRANSACTIONS][64];
    size_t response_len[MAX_EXPECTED_TRANSACTIONS];
    int send_count;
    int recv_count;
} FakeTransport;

static int fake_send(void *ctx, const uint8_t *data, size_t len)
{
    FakeTransport *fake = (FakeTransport *)ctx;
    int index = fake->send_count;

    assert(index >= 0);
    assert(index < (int)MAX_EXPECTED_TRANSACTIONS);
    assert(len == fake->expected_send_len[index]);
    assert(memcmp(data, fake->expected_send[index], len) == 0);
    fake->send_count++;
    return 0;
}

static int fake_recv(void *ctx,
                     uint8_t *data,
                     size_t data_cap,
                     size_t *data_len,
                     uint32_t timeout_ms)
{
    FakeTransport *fake = (FakeTransport *)ctx;
    int index = fake->recv_count;

    (void)timeout_ms;
    assert(index >= 0);
    assert(index < (int)MAX_EXPECTED_TRANSACTIONS);
    assert(fake->response_len[index] <= data_cap);
    memcpy(data, fake->responses[index], fake->response_len[index]);
    *data_len = fake->response_len[index];
    fake->recv_count++;
    return 0;
}

static UdsClient make_client(FakeTransport *fake)
{
    static const TransportOps ops = {
        fake_send,
        fake_recv,
    };
    UdsClient client = {0};

    uds_client_init(&client, &ops, fake);
    return client;
}

static void put_u32_be(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static void expect_request_download(FakeTransport *fake,
                                    int index,
                                    const uint8_t payload_id[PAYLOAD_ID_SIZE],
                                    uint32_t image_size,
                                    uint32_t resume_offset,
                                    uint8_t target_slot)
{
    uint8_t *send = fake->expected_send[index];
    uint8_t *response = fake->responses[index];

    send[0] = SID_REQUEST_DOWNLOAD;
    send[1] = DOWNLOAD_DATA_FORMAT_ID;
    send[2] = DOWNLOAD_ADDR_LEN_FORMAT_ID;
    put_u32_be(send + 3u, DOWNLOAD_MEMORY_ADDRESS);
    put_u32_be(send + 7u, image_size);
    memcpy(send + UDS_REQUEST_DOWNLOAD_PAYLOAD_ID_OFFSET,
           payload_id,
           PAYLOAD_ID_SIZE);
    fake->expected_send_len[index] = UDS_REQUEST_DOWNLOAD_REQUEST_LEN;

    response[0] = SID_REQUEST_DOWNLOAD_POS;
    response[1] = DOWNLOAD_MAX_BLOCK_LEN_FORMAT_ID;
    response[UDS_REQUEST_DOWNLOAD_RESPONSE_MAX_BLOCK_OFFSET] =
        (uint8_t)(TRANSFER_MAX_BLOCK_LENGTH >> 8);
    response[UDS_REQUEST_DOWNLOAD_RESPONSE_MAX_BLOCK_OFFSET + 1U] =
        (uint8_t)TRANSFER_MAX_BLOCK_LENGTH;
    response[UDS_REQUEST_DOWNLOAD_RESPONSE_TARGET_SLOT_OFFSET] = target_slot;
    put_u32_be(response + UDS_REQUEST_DOWNLOAD_RESPONSE_RESUME_OFFSET,
               resume_offset);
    fake->response_len[index] = UDS_REQUEST_DOWNLOAD_RESPONSE_LEN;
}

static void expect_prepare_download(FakeTransport *fake,
                                    int index,
                                    const uint8_t payload_id[PAYLOAD_ID_SIZE],
                                    uint32_t image_size)
{
    uint8_t *send = fake->expected_send[index];
    uint8_t *response = fake->responses[index];

    send[0] = SID_ROUTINE_CONTROL;
    send[1] = ROUTINE_CONTROL_START;
    send[2] = (uint8_t)(ROUTINE_ID_PREPARE_DOWNLOAD >> 8);
    send[3] = (uint8_t)ROUTINE_ID_PREPARE_DOWNLOAD;
    put_u32_be(send + UDS_PREPARE_DOWNLOAD_ROUTINE_SIZE_OFFSET, image_size);
    memcpy(send + UDS_PREPARE_DOWNLOAD_ROUTINE_PAYLOAD_ID_OFFSET,
           payload_id,
           PAYLOAD_ID_SIZE);
    fake->expected_send_len[index] = UDS_PREPARE_DOWNLOAD_ROUTINE_REQUEST_LEN;
    response[0] = SID_ROUTINE_CONTROL_POS;
    response[1] = ROUTINE_CONTROL_START;
    response[2] = send[2];
    response[3] = send[3];
    fake->response_len[index] = UDS_ROUTINE_CONTROL_REQUEST_LEN;
}

static void expect_prepare_download_ready(FakeTransport *fake, int index)
{
    uint8_t *send = fake->expected_send[index];
    uint8_t *response = fake->responses[index];

    send[0] = SID_ROUTINE_CONTROL;
    send[1] = ROUTINE_CONTROL_REQUEST_RESULTS;
    send[2] = (uint8_t)(ROUTINE_ID_PREPARE_DOWNLOAD >> 8);
    send[3] = (uint8_t)ROUTINE_ID_PREPARE_DOWNLOAD;
    fake->expected_send_len[index] = UDS_ROUTINE_CONTROL_REQUEST_LEN;
    response[0] = SID_ROUTINE_CONTROL_POS;
    response[1] = ROUTINE_CONTROL_REQUEST_RESULTS;
    response[2] = send[2];
    response[3] = send[3];
    response[4] = ROUTINE_PREPARE_DOWNLOAD_STATUS_READY;
    fake->response_len[index] = UDS_PREPARE_DOWNLOAD_ROUTINE_RESULT_LEN;
}

static void expect_transfer(FakeTransport *fake,
                            int index,
                            uint8_t sequence,
                            const uint8_t *data,
                            uint16_t length)
{
    fake->expected_send[index][0] = SID_TRANSFER_DATA;
    fake->expected_send[index][1] = sequence;
    memcpy(fake->expected_send[index] + 2u, data, length);
    fake->expected_send_len[index] = (size_t)length + 2u;
    fake->responses[index][0] = SID_TRANSFER_DATA_POS;
    fake->responses[index][1] = sequence;
    fake->response_len[index] = 2u;
}

static void expect_transfer_exit(FakeTransport *fake, int index)
{
    fake->expected_send[index][0] = SID_REQUEST_TRANSFER_EXIT;
    fake->expected_send_len[index] = 1u;
    fake->responses[index][0] = SID_REQUEST_TRANSFER_EXIT_POS;
    fake->response_len[index] = 1u;
}

static void fill_image(uint8_t *image, size_t length)
{
    for (size_t index = 0u; index < length; index++)
    {
        image[index] = (uint8_t)index;
    }
}

static void image_payload_id(const uint8_t *image, uint32_t image_size,
                             uint8_t payload_id[PAYLOAD_ID_SIZE])
{
    sha256_compute(image, image_size, payload_id);
}

static int expect_transfer_range(FakeTransport *fake,
                                 int index,
                                 const uint8_t *image,
                                 uint32_t start,
                                 uint32_t end)
{
    uint32_t offset = start;
    uint8_t sequence = 0x01u;

    while (offset < end)
    {
        uint32_t remaining = end - offset;
        uint16_t length = (uint16_t)(remaining > TRANSFER_BLOCK_PAYLOAD
                                         ? TRANSFER_BLOCK_PAYLOAD
                                         : remaining);
        expect_transfer(fake, index, sequence, image + offset, length);
        offset += length;
        sequence++;
        index++;
    }
    return index;
}

static void test_uds_download_primitives(void)
{
    FakeTransport fake = {0};
    UdsClient client = make_client(&fake);
    UdsDownloadResponse response = {0};
    uint8_t payload_id[PAYLOAD_ID_SIZE] = {0};
    uint8_t block[TRANSFER_BLOCK_PAYLOAD] = {0};

    for (size_t index = 0u; index < sizeof(block); index++)
    {
        block[index] = (uint8_t)index;
    }
    payload_id[0] = 0xA5u;
    expect_request_download(&fake,
                            0,
                            payload_id,
                            sizeof(block),
                            0u,
                            OTA_SLOT_B);
    expect_transfer(&fake, 1, 0x01u, block, sizeof(block));
    expect_transfer_exit(&fake, 2);

    assert(uds_request_download(&client,
                                payload_id,
                                sizeof(block),
                                &response) == 0);
    assert(response.max_block_len == TRANSFER_MAX_BLOCK_LENGTH);
    assert(response.target_slot == OTA_SLOT_B);
    assert(response.resume_offset == 0u);
    assert(uds_transfer_data(&client, 0x01u, block, sizeof(block)) == 0);
    assert(uds_request_transfer_exit(&client) == 0);
}

static void test_uds_prepare_download_primitives(void)
{
    FakeTransport fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t payload_id[PAYLOAD_ID_SIZE] = {0};
    bool ready = false;

    payload_id[0] = 0xA5u;
    expect_prepare_download(&fake, 0, payload_id, 256u);
    expect_prepare_download_ready(&fake, 1);
    fake.responses[1][4] = ROUTINE_PREPARE_DOWNLOAD_STATUS_PENDING;
    expect_prepare_download_ready(&fake, 2);

    assert(uds_prepare_download(&client, payload_id, 256u) == 0);
    assert(uds_prepare_download_ready(&client, &ready) == 0);
    assert(!ready);
    assert(uds_prepare_download_ready(&client, &ready) == 0);
    assert(ready);
}

static void test_uds_request_download_negative_response(void)
{
    FakeTransport fake = {0};
    UdsClient client = make_client(&fake);
    UdsDownloadResponse response = {0};
    uint8_t payload_id[PAYLOAD_ID_SIZE] = {0};

    expect_request_download(&fake, 0, payload_id, 0U, 0U, OTA_SLOT_B);
    fake.responses[0][0] = NEGATIVE_RESPONSE_SID;
    fake.responses[0][1] = SID_REQUEST_DOWNLOAD;
    fake.responses[0][2] = NRC_REQUEST_OUT_OF_RANGE;
    fake.response_len[0] = 3U;

    assert(uds_request_download(&client, payload_id, 0U, &response) ==
           UDS_ERR_NEGATIVE_RESPONSE);
    assert(client.last_nrc == NRC_REQUEST_OUT_OF_RANGE);
}

static void test_uds_request_download_length_mismatch(void)
{
    FakeTransport fake = {0};
    UdsClient client = make_client(&fake);
    UdsDownloadResponse response = {0};
    uint8_t payload_id[PAYLOAD_ID_SIZE] = {0};

    expect_request_download(&fake, 0, payload_id, 512U, 0U, OTA_SLOT_B);
    fake.response_len[0] = 3U;

    assert(uds_request_download(&client, payload_id, 512U, &response) ==
           UDS_ERR_MALFORMED_RESPONSE);
}

static void test_uds_transfer_wrong_block_sequence_nrc(void)
{
    FakeTransport fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t block[TRANSFER_BLOCK_PAYLOAD] = {0};

    expect_transfer(&fake, 0, 0x02U, block, sizeof(block));
    fake.responses[0][0] = NEGATIVE_RESPONSE_SID;
    fake.responses[0][1] = SID_TRANSFER_DATA;
    fake.responses[0][2] = NRC_WRONG_BLOCK_SEQUENCE_COUNTER;
    fake.response_len[0] = 3U;

    assert(uds_transfer_data(&client,
                             0x02U,
                             block,
                             sizeof(block)) ==
           UDS_ERR_NEGATIVE_RESPONSE);
    assert(client.last_nrc == NRC_WRONG_BLOCK_SEQUENCE_COUNTER);
}

static void test_execute_fresh_transfer(void)
{
    FakeTransport fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t image[TEST_IMAGE_SIZE] = {0};
    uint8_t payload_id[PAYLOAD_ID_SIZE];
    uint8_t target_slot = OTA_SLOT_A;
    int next_index = 0;

    fill_image(image, sizeof(image));
    image_payload_id(image, TEST_IMAGE_SIZE, payload_id);
    expect_prepare_download(&fake, 0, payload_id, TEST_IMAGE_SIZE);
    expect_prepare_download_ready(&fake, 1);
    expect_request_download(&fake,
                             2,
                            payload_id,
                            TEST_IMAGE_SIZE,
                            0u,
                            OTA_SLOT_B);
    next_index = expect_transfer_range(&fake,
                                       3,
                                       image,
                                       0u,
                                       TEST_IMAGE_SIZE);
    expect_transfer_exit(&fake, next_index);

    assert(resume_transfer_execute(&client,
                                   payload_id,
                                   TEST_IMAGE_SIZE,
                                   image,
                                   &target_slot) == 0);
    assert(target_slot == OTA_SLOT_B);
    assert(fake.send_count == next_index + 1);
}

static void test_execute_resumed_transfer(void)
{
    FakeTransport fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t image[TEST_IMAGE_SIZE] = {0};
    uint8_t payload_id[PAYLOAD_ID_SIZE];
    uint8_t target_slot = OTA_SLOT_A;
    int next_index = 0;

    fill_image(image, sizeof(image));
    image_payload_id(image, TEST_IMAGE_SIZE, payload_id);
    expect_prepare_download(&fake, 0, payload_id, TEST_IMAGE_SIZE);
    expect_prepare_download_ready(&fake, 1);
    expect_request_download(&fake,
                             2,
                            payload_id,
                            TEST_IMAGE_SIZE,
                            META_CHECKPOINT_INTERVAL,
                            OTA_SLOT_B);
    next_index = expect_transfer_range(&fake,
                                       3,
                                       image,
                                       META_CHECKPOINT_INTERVAL,
                                       TEST_IMAGE_SIZE);
    expect_transfer_exit(&fake, next_index);

    assert(resume_transfer_execute(&client,
                                   payload_id,
                                   TEST_IMAGE_SIZE,
                                   image,
                                   &target_slot) == 0);
    assert(target_slot == OTA_SLOT_B);
    assert(fake.send_count == next_index + 1);
}

static void test_execute_already_complete(void)
{
    FakeTransport fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t image[TEST_IMAGE_SIZE] = {0};
    uint8_t payload_id[PAYLOAD_ID_SIZE];
    uint8_t target_slot = OTA_SLOT_A;

    fill_image(image, sizeof(image));
    image_payload_id(image, TEST_IMAGE_SIZE, payload_id);
    expect_prepare_download(&fake, 0, payload_id, TEST_IMAGE_SIZE);
    expect_prepare_download_ready(&fake, 1);
    expect_request_download(&fake,
                             2,
                            payload_id,
                            TEST_IMAGE_SIZE,
                            TEST_IMAGE_SIZE,
                            OTA_SLOT_B);

    assert(resume_transfer_execute(&client,
                                   payload_id,
                                   TEST_IMAGE_SIZE,
                                   image,
                                   &target_slot) == 0);
    assert(target_slot == OTA_SLOT_B);
    assert(fake.send_count == 3);
}

static void test_invalid_resume_response_is_rejected(void)
{
    FakeTransport fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t image[TEST_IMAGE_SIZE] = {0};
    uint8_t payload_id[PAYLOAD_ID_SIZE];
    uint8_t target_slot = OTA_SLOT_A;

    fill_image(image, sizeof(image));
    image_payload_id(image, TEST_IMAGE_SIZE, payload_id);
    expect_prepare_download(&fake, 0, payload_id, TEST_IMAGE_SIZE);
    expect_prepare_download_ready(&fake, 1);
    expect_request_download(&fake,
                             2,
                            payload_id,
                            TEST_IMAGE_SIZE,
                            META_CHECKPOINT_INTERVAL / 2u,
                            OTA_SLOT_B);

    assert(resume_transfer_execute(&client,
                                   payload_id,
                                   TEST_IMAGE_SIZE,
                                   image,
                                   &target_slot) ==
           RESUME_TRANSFER_ERR_CHECKPOINT_ALIGNMENT);
    assert(fake.send_count == 3);
}

static void test_invalid_target_slot_is_rejected(void)
{
    FakeTransport fake = {0};
    UdsClient client = make_client(&fake);
    uint8_t image[TEST_IMAGE_SIZE] = {0};
    uint8_t payload_id[PAYLOAD_ID_SIZE];
    uint8_t target_slot = OTA_SLOT_A;

    fill_image(image, sizeof(image));
    image_payload_id(image, TEST_IMAGE_SIZE, payload_id);
    expect_prepare_download(&fake, 0, payload_id, TEST_IMAGE_SIZE);
    expect_prepare_download_ready(&fake, 1);
    expect_request_download(&fake,
                             2,
                            payload_id,
                            TEST_IMAGE_SIZE,
                            0u,
                            2u);

    assert(resume_transfer_execute(&client,
                                   payload_id,
                                   TEST_IMAGE_SIZE,
                                   image,
                                   &target_slot) ==
           RESUME_TRANSFER_ERR_IDENTITY_MISMATCH);
    assert(fake.send_count == 3);
}

static void test_image_changes_payload_id(void)
{
    uint8_t image_a[TEST_IMAGE_SIZE] = {0};
    uint8_t image_b[TEST_IMAGE_SIZE] = {0};
    uint8_t payload_id_a[PAYLOAD_ID_SIZE];
    uint8_t payload_id_b[PAYLOAD_ID_SIZE];

    fill_image(image_a, sizeof(image_a));
    fill_image(image_b, sizeof(image_b));
    image_b[20] ^= 0x01u;
    image_payload_id(image_a, TEST_IMAGE_SIZE, payload_id_a);
    image_payload_id(image_b, TEST_IMAGE_SIZE, payload_id_b);
    assert(memcmp(payload_id_a, payload_id_b, PAYLOAD_ID_SIZE) != 0);
}

int main(void)
{
    test_uds_download_primitives();
    test_uds_prepare_download_primitives();
    test_uds_request_download_negative_response();
    test_uds_request_download_length_mismatch();
    test_uds_transfer_wrong_block_sequence_nrc();
    test_execute_fresh_transfer();
    test_execute_resumed_transfer();
    test_execute_already_complete();
    test_invalid_resume_response_is_rejected();
    test_invalid_target_slot_is_rejected();
    test_image_changes_payload_id();
    return 0;
}
