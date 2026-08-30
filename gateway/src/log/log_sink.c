#include "log_sink.h"

void log_sink_init(LogSink_t *sink, FILE *stream)
{
    if (sink == NULL) {
        return;
    }

    sink->stream = stream;
}

int log_sink_write(LogSink_t *sink, const LogRecordView_t *record)
{
    if (sink == NULL || sink->stream == NULL || record == NULL ||
        (record->length > 0u && record->data == NULL) ||
        record->length > LOG_RECEIVER_MAX_LINE_SIZE) {
        return -1;
    }

    if (record->length > 0u &&
        fwrite(record->data, 1u, record->length, sink->stream) !=
            record->length) {
        return -1;
    }
    if (record->length == 0u ||
        record->data[record->length - 1u] != '\n') {
        if (fputc('\n', sink->stream) == EOF) {
            return -1;
        }
    }

    return fflush(sink->stream) == 0 ? 0 : -1;
}
