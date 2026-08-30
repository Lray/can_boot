#ifndef LOG_SINK_H
#define LOG_SINK_H

#include <stdio.h>

#include "log_receiver.h"

typedef struct {
    FILE *stream;
} LogSink_t;

/* Configures a sink around an already opened output stream. */
void log_sink_init(LogSink_t *sink, FILE *stream);

/* Writes one completed record and flushes the stream for live output. */
int log_sink_write(LogSink_t *sink, const LogRecordView_t *record);

#endif /* LOG_SINK_H */
