#ifndef LOG_RECEIVER_STATS_H
#define LOG_RECEIVER_STATS_H

#include <stdint.h>

/* Counters owned by the application, not by the frame or reassembly state. */
typedef struct {
    uint32_t abandoned_records;
    uint32_t rejected_frames;
} LogReceiverStats_t;

#endif /* LOG_RECEIVER_STATS_H */
