#include "log_receiver.h"

#include <string.h>

static void log_receiver_reset(LogReceiver_t *receiver)
{
    receiver->active = false;
    receiver->line_length = 0u;
    receiver->next_fragment = 0u;
}

static LogReceiverResult_t log_receiver_result(
    LogReceiverStatus_t status,
    bool abandoned_record)
{
    LogReceiverResult_t result = {0};

    result.status = status;
    result.abandoned_record = abandoned_record;
    return result;
}

void log_receiver_init(LogReceiver_t *receiver)
{
    if (receiver == NULL) {
        return;
    }

    (void)memset(receiver, 0, sizeof(*receiver));
}

bool log_receiver_abort(LogReceiver_t *receiver)
{
    bool abandoned_record = false;

    if (receiver == NULL) {
        return false;
    }

    abandoned_record = receiver->active;
    log_receiver_reset(receiver);
    return abandoned_record;
}

LogReceiverResult_t log_receiver_accept(
    LogReceiver_t *receiver,
    const EcuUlogFrame_t *frame,
    LogRecordView_t *record_out)
{
    uint8_t fragment_index = 0u;
    bool abandoned_record = false;

    if (record_out != NULL) {
        record_out->data = NULL;
        record_out->length = 0u;
    }
    if (receiver == NULL || frame == NULL || record_out == NULL) {
        return log_receiver_result(LOG_RECEIVER_REJECTED, false);
    }

    fragment_index = log_frame_fragment_index(frame);
    if (log_frame_is_start(frame)) {
        abandoned_record = receiver->active;
        log_receiver_reset(receiver);
        if (fragment_index != 0u) {
            return log_receiver_result(
                LOG_RECEIVER_REJECTED,
                abandoned_record);
        }

        receiver->active = true;
        receiver->next_fragment = 0u;
    }

    if (fragment_index >= ECU_ULOG_MAX_FRAGMENTS ||
        receiver->active == false ||
        receiver->next_fragment != fragment_index ||
        frame->payload_length > ECU_ULOG_FRAME_PAYLOAD_SIZE ||
        (receiver->line_length + frame->payload_length) >
            LOG_RECEIVER_MAX_LINE_SIZE) {
        abandoned_record = abandoned_record || receiver->active;
        log_receiver_reset(receiver);
        return log_receiver_result(
            LOG_RECEIVER_REJECTED,
            abandoned_record);
    }

    if (frame->payload_length > 0u) {
        (void)memcpy(
            &receiver->line[receiver->line_length],
            frame->payload,
            frame->payload_length);
        receiver->line_length += frame->payload_length;
    }
    receiver->next_fragment++;

    if (!log_frame_is_end(frame)) {
        return log_receiver_result(
            LOG_RECEIVER_INCOMPLETE,
            abandoned_record);
    }

    receiver->line[receiver->line_length] = '\0';
    record_out->data = receiver->line;
    record_out->length = receiver->line_length;
    receiver->active = false;
    receiver->next_fragment = 0u;
    return log_receiver_result(LOG_RECEIVER_COMPLETE, abandoned_record);
}
