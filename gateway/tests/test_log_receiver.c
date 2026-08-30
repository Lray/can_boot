#include <assert.h>
#include <linux/can.h>
#include <string.h>

#include "log_frame.h"
#include "log_receiver.h"

static struct can_frame make_frame(
    uint8_t control,
    const char *payload,
    uint8_t payload_length)
{
    struct can_frame frame = {0};

    assert(payload_length <= ECU_ULOG_FRAME_PAYLOAD_SIZE);
    frame.can_id = ECU_ULOG_CAN_ID;
    frame.can_dlc = (uint8_t)(ECU_ULOG_FRAME_HEADER_SIZE + payload_length);
    frame.data[0] = control;
    if (payload_length > 0u) {
        (void)memcpy(&frame.data[ECU_ULOG_FRAME_HEADER_SIZE],
                     payload,
                     payload_length);
    }
    return frame;
}

static LogReceiverResult_t accept_raw(
    LogReceiver_t *receiver,
    const struct can_frame *raw_frame,
    LogRecordView_t *record)
{
    EcuUlogFrame_t frame = {0};

    assert(log_frame_decode(raw_frame, &frame) == LOG_FRAME_VALID);
    return log_receiver_accept(receiver, &frame, record);
}

static void test_reassembles_complete_record(void)
{
    LogReceiver_t receiver = {0};
    LogRecordView_t record = {0};
    struct can_frame first = make_frame(0x80u, "hello ", 6u);
    struct can_frame final = make_frame(0x41u, "world", 5u);
    LogReceiverResult_t result = {0};

    log_receiver_init(&receiver);
    result = accept_raw(&receiver, &first, &record);
    assert(result.status == LOG_RECEIVER_INCOMPLETE);
    result = accept_raw(&receiver, &final, &record);
    assert(result.status == LOG_RECEIVER_COMPLETE);
    assert(record.length == strlen("hello world"));
    assert(strcmp(record.data, "hello world") == 0);
}

static void test_discards_out_of_order_fragment(void)
{
    LogReceiver_t receiver = {0};
    LogRecordView_t record = {0};
    struct can_frame first = make_frame(0x80u, "part", 4u);
    struct can_frame final = make_frame(0x42u, "bad", 3u);
    LogReceiverResult_t result = {0};

    log_receiver_init(&receiver);
    result = accept_raw(&receiver, &first, &record);
    assert(result.status == LOG_RECEIVER_INCOMPLETE);
    result = accept_raw(&receiver, &final, &record);
    assert(result.status == LOG_RECEIVER_REJECTED);
    assert(result.abandoned_record);
}

static void test_new_start_abandons_previous_record(void)
{
    LogReceiver_t receiver = {0};
    LogRecordView_t record = {0};
    struct can_frame abandoned = make_frame(0x80u, "old", 3u);
    struct can_frame next = make_frame(0xC0u, "new", 3u);
    LogReceiverResult_t result = {0};

    log_receiver_init(&receiver);
    result = accept_raw(&receiver, &abandoned, &record);
    assert(result.status == LOG_RECEIVER_INCOMPLETE);
    result = accept_raw(&receiver, &next, &record);
    assert(result.status == LOG_RECEIVER_COMPLETE);
    assert(result.abandoned_record);
    assert(strcmp(record.data, "new") == 0);
}

static void test_accepts_mcu_protocol_maximum_record(void)
{
    LogReceiver_t receiver = {0};
    LogRecordView_t record = {0};
    char payload[ECU_ULOG_FRAME_PAYLOAD_SIZE] = {0};

    log_receiver_init(&receiver);
    for (unsigned int index = 0u;
         index < ECU_ULOG_MAX_FRAGMENTS;
         index++) {
        uint8_t control = (uint8_t)index;
        struct can_frame frame;
        LogReceiverResult_t result;

        if (index == 0u) {
            control = (uint8_t)(control | ECU_ULOG_CONTROL_START);
        }
        if (index == (ECU_ULOG_MAX_FRAGMENTS - 1u)) {
            control = (uint8_t)(control | ECU_ULOG_CONTROL_END);
        }
        (void)memset(payload, (int)('A' + (index % 26u)), sizeof(payload));
        frame = make_frame(control, payload, sizeof(payload));
        result = accept_raw(&receiver, &frame, &record);
        if (index + 1u < ECU_ULOG_MAX_FRAGMENTS) {
            assert(result.status == LOG_RECEIVER_INCOMPLETE);
        } else {
            assert(result.status == LOG_RECEIVER_COMPLETE);
        }
    }
    assert(record.length == ECU_ULOG_MAX_LOG_SIZE);
}

static void test_frame_decoder_rejects_invalid_length(void)
{
    EcuUlogFrame_t decoded = {0};
    struct can_frame frame = make_frame(0xC0u, "x", 1u);

    frame.can_dlc = 0u;
    assert(log_frame_decode(&frame, &decoded) == LOG_FRAME_MALFORMED);
}

int main(void)
{
    test_reassembles_complete_record();
    test_discards_out_of_order_fragment();
    test_new_start_abandons_previous_record();
    test_accepts_mcu_protocol_maximum_record();
    test_frame_decoder_rejects_invalid_length();
    return 0;
}
