#ifndef LOG_RECEIVER_H
#define LOG_RECEIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "log_frame.h"

/* The MCU wire contract permits 64 seven-byte fragments. */
#define LOG_RECEIVER_MAX_LINE_SIZE ECU_ULOG_MAX_LOG_SIZE

/* A non-owning view of one completely reassembled ULog record. */
typedef struct {
    const char *data;
    size_t length;
} LogRecordView_t;

/* Reassembly state only; output and counters live outside this object. */
typedef struct {
    char line[LOG_RECEIVER_MAX_LINE_SIZE + 1u];
    size_t line_length;
    uint8_t next_fragment;
    bool active;
} LogReceiver_t;

typedef enum {
    LOG_RECEIVER_REJECTED = -1,
    LOG_RECEIVER_INCOMPLETE = 0,
    LOG_RECEIVER_COMPLETE = 1
} LogReceiverStatus_t;

typedef struct {
    LogReceiverStatus_t status;
    bool abandoned_record;
} LogReceiverResult_t;

/* Initializes empty ULog fragment reassembly state. */
void log_receiver_init(LogReceiver_t *receiver);

/* Discards the active record and reports whether one was abandoned. */
bool log_receiver_abort(LogReceiver_t *receiver);

/*
 * Consumes one already decoded ULog frame.  The returned view is valid until
 * the next receiver call.  This function never performs output or counting.
 */
LogReceiverResult_t log_receiver_accept(
    LogReceiver_t *receiver,
    const EcuUlogFrame_t *frame,
    LogRecordView_t *record_out);

#endif /* LOG_RECEIVER_H */
