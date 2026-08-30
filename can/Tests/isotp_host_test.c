#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "can_frame.h"
#include "isotp.h"
#include "shared/can_network.h"

#define TEST_FRAME_CAPACITY 16U
#define TEST_PAYLOAD_CAPACITY 512U

typedef struct
{
    uint32_t id;
    uint8_t data[CAN_CLASSIC_MAX_DLC];
    uint8_t length;
} test_frame_t;

static IsoTpLink s_link;
static uint8_t s_send_buffer[TEST_PAYLOAD_CAPACITY];
static uint8_t s_receive_buffer[TEST_PAYLOAD_CAPACITY];
static uint8_t s_received_payload[TEST_PAYLOAD_CAPACITY];
static uint32_t s_received_length;
static uint32_t s_received_count;
static test_frame_t s_sent_frames[TEST_FRAME_CAPACITY];
static uint32_t s_sent_count;
static uint32_t s_time_us;
static int s_send_result;

#if ISO_TP_DEFAULT_BLOCK_SIZE != ISOTP_BLOCK_SIZE
#error "ISO-TP block size differs from the shared product profile"
#endif

#if ISO_TP_DEFAULT_ST_MIN_US != (ISOTP_STMIN_MS * 1000U)
#error "ISO-TP STmin differs from the shared product profile"
#endif

int isotp_user_send_can(const uint32_t arbitration_id,
                        const uint8_t *data,
                        const uint8_t size)
{
    assert(data != NULL);
    assert(size <= CAN_CLASSIC_MAX_DLC);
    if (s_send_result != ISOTP_RET_OK)
    {
        return s_send_result;
    }

    assert(s_sent_count < TEST_FRAME_CAPACITY);
    s_sent_frames[s_sent_count].id = arbitration_id;
    s_sent_frames[s_sent_count].length = size;
    memcpy(s_sent_frames[s_sent_count].data, data, size);
    s_sent_count++;
    return ISOTP_RET_OK;
}

uint32_t isotp_user_get_us(void)
{
    return s_time_us;
}

void isotp_user_debug(const char *message, ...)
{
    (void)message;
}

static void CaptureMessage(void *link,
                           const uint8_t *payload,
                           uint32_t length,
                           void *context)
{
    (void)link;
    (void)context;
    assert(length <= sizeof(s_received_payload));
    memcpy(s_received_payload, payload, length);
    s_received_length = length;
    s_received_count++;
}

static void ResetTest(void)
{
    memset(&s_link, 0, sizeof(s_link));
    memset(s_send_buffer, 0, sizeof(s_send_buffer));
    memset(s_receive_buffer, 0, sizeof(s_receive_buffer));
    memset(s_received_payload, 0, sizeof(s_received_payload));
    memset(s_sent_frames, 0, sizeof(s_sent_frames));
    s_received_length = 0U;
    s_received_count = 0U;
    s_sent_count = 0U;
    s_time_us = 0U;
    s_send_result = ISOTP_RET_OK;
    isotp_init_link(&s_link,
                    CAN_ID_UDS_RESPONSE,
                    s_send_buffer,
                    sizeof(s_send_buffer),
                    s_receive_buffer,
                    sizeof(s_receive_buffer));
    isotp_set_rx_done_cb(&s_link, CaptureMessage, NULL);
}

static void TestReceiveSingleFrameWithoutPadding(void)
{
    const uint8_t frame[] = {0x03U, 0x11U, 0x22U, 0x33U};

    ResetTest();
    isotp_on_can_message(&s_link, frame, sizeof(frame));
    assert(s_received_count == 1U);
    assert(s_received_length == 3U);
    assert(memcmp(s_received_payload, &frame[1], 3U) == 0);
}

static void TestReceiveMultiFrameUsesProductFlowControl(void)
{
    uint8_t first_frame[CAN_CLASSIC_MAX_DLC] = {0x10U, 20U};
    uint8_t consecutive_frame[CAN_CLASSIC_MAX_DLC] = {0};

    for (uint8_t index = 0U; index < 6U; index++)
    {
        first_frame[index + 2U] = index;
    }

    ResetTest();
    isotp_on_can_message(&s_link, first_frame, sizeof(first_frame));
    assert(s_sent_count == 1U);
    assert(s_sent_frames[0].id == CAN_ID_UDS_RESPONSE);
    assert(s_sent_frames[0].length == CAN_CLASSIC_MAX_DLC);
    assert(s_sent_frames[0].data[0] == 0x30U);
    assert(s_sent_frames[0].data[1] == ISOTP_BLOCK_SIZE);
    assert(s_sent_frames[0].data[2] == ISOTP_STMIN_MS);

    consecutive_frame[0] = 0x21U;
    for (uint8_t index = 0U; index < 7U; index++)
    {
        consecutive_frame[index + 1U] = (uint8_t)(index + 6U);
    }
    isotp_on_can_message(&s_link,
                         consecutive_frame,
                         sizeof(consecutive_frame));

    consecutive_frame[0] = 0x22U;
    for (uint8_t index = 0U; index < 7U; index++)
    {
        consecutive_frame[index + 1U] = (uint8_t)(index + 13U);
    }
    isotp_on_can_message(&s_link,
                         consecutive_frame,
                         sizeof(consecutive_frame));

    assert(s_received_count == 1U);
    assert(s_received_length == 20U);
    for (uint8_t index = 0U; index < 20U; index++)
    {
        assert(s_received_payload[index] == index);
    }
}

static void TestReceiveProtocolErrorsRemainObservable(void)
{
    uint8_t first_frame[CAN_CLASSIC_MAX_DLC] = {0x10U, 9U};
    uint8_t wrong_sequence[CAN_CLASSIC_MAX_DLC] = {0x22U};

    ResetTest();
    isotp_on_can_message(&s_link, first_frame, sizeof(first_frame));
    isotp_on_can_message(&s_link, wrong_sequence, sizeof(wrong_sequence));
    assert(s_received_count == 0U);
    assert(s_link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
    assert(s_link.receive_protocol_result == ISOTP_PROTOCOL_RESULT_WRONG_SN);
}

static void TestReceiveFinalConsecutiveFrameWithoutPadding(void)
{
    const uint8_t first_frame[CAN_CLASSIC_MAX_DLC] = {
        0x10U, 9U, 0xA0U, 0xA1U, 0xA2U, 0xA3U, 0xA4U, 0xA5U,
    };
    const uint8_t final_frame[] = {0x21U, 0xA6U, 0xA7U, 0xA8U};

    ResetTest();
    isotp_on_can_message(&s_link, first_frame, sizeof(first_frame));
    isotp_on_can_message(&s_link, final_frame, sizeof(final_frame));
    assert(s_received_count == 1U);
    assert(s_received_length == 9U);
    for (uint8_t index = 0U; index < 9U; index++)
    {
        assert(s_received_payload[index] == (uint8_t)(0xA0U + index));
    }
}

static void TestReceiveShortNonFinalConsecutiveFrameAborts(void)
{
    const uint8_t first_frame[CAN_CLASSIC_MAX_DLC] = {0x10U, 20U};
    const uint8_t short_frame[] = {0x21U, 0xAAU};

    ResetTest();
    isotp_on_can_message(&s_link, first_frame, sizeof(first_frame));
    isotp_on_can_message(&s_link, short_frame, sizeof(short_frame));
    assert(s_received_count == 0U);
    assert(s_link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
}

static void TestReceiveOverflowSendsOverflowFlowControl(void)
{
    const uint8_t first_frame[CAN_CLASSIC_MAX_DLC] = {0x12U, 0x01U};

    ResetTest();
    isotp_on_can_message(&s_link, first_frame, sizeof(first_frame));
    assert(s_received_count == 0U);
    assert(s_sent_count == 1U);
    assert(s_sent_frames[0].data[0] == 0x32U);
    assert(s_link.receive_protocol_result ==
           ISOTP_PROTOCOL_RESULT_BUFFER_OVFLW);
}

static void TestSendSingleFrameUsesConfiguredPadding(void)
{
    const uint8_t payload[] = {0x10U, 0x20U, 0x30U};

    ResetTest();
    assert(isotp_send(&s_link, payload, sizeof(payload)) == ISOTP_RET_OK);
    assert(s_sent_count == 1U);
    assert(s_sent_frames[0].length == CAN_CLASSIC_MAX_DLC);
    assert(s_sent_frames[0].data[0] == sizeof(payload));
    assert(memcmp(&s_sent_frames[0].data[1], payload, sizeof(payload)) == 0);
    for (uint8_t index = 4U; index < CAN_CLASSIC_MAX_DLC; index++)
    {
        assert(s_sent_frames[0].data[index] == 0U);
    }
}

static void TestSendEnforcesConfiguredPayloadCapacity(void)
{
    uint8_t payload[TEST_PAYLOAD_CAPACITY + 1U] = {0};

    ResetTest();
    assert(isotp_send(&s_link, payload, TEST_PAYLOAD_CAPACITY) == ISOTP_RET_OK);
    assert(s_sent_count == 1U);
    assert(s_sent_frames[0].data[0] == 0x12U);
    assert(s_sent_frames[0].data[1] == 0x00U);

    ResetTest();
    assert(isotp_send(&s_link, payload, sizeof(payload)) == ISOTP_RET_OVERFLOW);
    assert(s_sent_count == 0U);
}

static void TestSendHonorsBlockSizeAndStmin(void)
{
    uint8_t payload[22U] = {0};
    uint8_t flow_control[CAN_CLASSIC_MAX_DLC] = {0x30U, 2U, 2U};

    ResetTest();
    assert(isotp_send(&s_link, payload, sizeof(payload)) == ISOTP_RET_OK);
    assert(s_sent_count == 1U);
    assert(s_link.send_status == ISOTP_SEND_STATUS_INPROGRESS);

    isotp_on_can_message(&s_link, flow_control, sizeof(flow_control));
    s_time_us = 1U;
    isotp_poll(&s_link);
    assert(s_sent_count == 2U);
    assert(s_sent_frames[1].data[0] == 0x21U);

    s_time_us = 2001U;
    isotp_poll(&s_link);
    assert(s_sent_count == 2U);
    s_time_us = 2002U;
    isotp_poll(&s_link);
    assert(s_sent_count == 3U);
    assert(s_sent_frames[2].data[0] == 0x22U);
    assert(s_link.send_status == ISOTP_SEND_STATUS_INPROGRESS);

    flow_control[1] = 0U;
    isotp_on_can_message(&s_link, flow_control, sizeof(flow_control));
    s_time_us = 4003U;
    isotp_poll(&s_link);
    assert(s_sent_count == 4U);
    assert(s_sent_frames[3].data[0] == 0x23U);
    assert(s_link.send_status == ISOTP_SEND_STATUS_IDLE);
}

static void TestProtocolTimeoutsReportStandardReason(void)
{
    uint8_t payload[8U] = {0};
    uint8_t first_frame[CAN_CLASSIC_MAX_DLC] = {0x10U, 9U};

    ResetTest();
    assert(isotp_send(&s_link, payload, sizeof(payload)) == ISOTP_RET_OK);
    s_time_us = ISO_TP_DEFAULT_RESPONSE_TIMEOUT_US + 1U;
    isotp_poll(&s_link);
    assert(s_link.send_status == ISOTP_SEND_STATUS_ERROR);
    assert(s_link.send_protocol_result == ISOTP_PROTOCOL_RESULT_TIMEOUT_BS);

    ResetTest();
    isotp_on_can_message(&s_link, first_frame, sizeof(first_frame));
    s_time_us = ISO_TP_DEFAULT_RESPONSE_TIMEOUT_US + 1U;
    isotp_poll(&s_link);
    assert(s_link.receive_status == ISOTP_RECEIVE_STATUS_IDLE);
    assert(s_link.receive_protocol_result == ISOTP_PROTOCOL_RESULT_TIMEOUT_CR);
}

static void TestConsecutiveFrameRetriesNoSpace(void)
{
    uint8_t payload[8U] = {0};
    const uint8_t flow_control[CAN_CLASSIC_MAX_DLC] = {0x30U, 0U, 0U};

    ResetTest();
    assert(isotp_send(&s_link, payload, sizeof(payload)) == ISOTP_RET_OK);
    isotp_on_can_message(&s_link, flow_control, sizeof(flow_control));

    s_send_result = ISOTP_RET_NOSPACE;
    s_time_us = 1U;
    isotp_poll(&s_link);
    assert(s_sent_count == 1U);
    assert(s_link.send_status == ISOTP_SEND_STATUS_INPROGRESS);

    s_send_result = ISOTP_RET_OK;
    s_time_us = 2U;
    isotp_poll(&s_link);
    assert(s_sent_count == 2U);
    assert(s_link.send_status == ISOTP_SEND_STATUS_IDLE);
}

static void TestFlowControlWaitLimit(void)
{
    uint8_t payload[8U] = {0};
    const uint8_t wait_frame[CAN_CLASSIC_MAX_DLC] = {0x31U, 0U, 0U};

    ResetTest();
    assert(isotp_send(&s_link, payload, sizeof(payload)) == ISOTP_RET_OK);
    for (uint8_t count = 0U; count <= ISO_TP_MAX_WFT_NUMBER; count++)
    {
        isotp_on_can_message(&s_link, wait_frame, sizeof(wait_frame));
    }
    assert(s_link.send_status == ISOTP_SEND_STATUS_ERROR);
    assert(s_link.send_protocol_result == ISOTP_PROTOCOL_RESULT_WFT_OVRN);
}

int main(void)
{
    TestReceiveSingleFrameWithoutPadding();
    TestReceiveMultiFrameUsesProductFlowControl();
    TestReceiveProtocolErrorsRemainObservable();
    TestReceiveFinalConsecutiveFrameWithoutPadding();
    TestReceiveShortNonFinalConsecutiveFrameAborts();
    TestReceiveOverflowSendsOverflowFlowControl();
    TestSendSingleFrameUsesConfiguredPadding();
    TestSendEnforcesConfiguredPayloadCapacity();
    TestSendHonorsBlockSizeAndStmin();
    TestProtocolTimeoutsReportStandardReason();
    TestConsecutiveFrameRetriesNoSpace();
    TestFlowControlWaitLimit();
    return 0;
}
