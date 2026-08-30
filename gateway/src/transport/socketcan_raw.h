#ifndef SOCKETCAN_RAW_H
#define SOCKETCAN_RAW_H

#include <linux/can.h>
#include <stddef.h>
#include <stdint.h>

#define SOCKETCAN_RAW_TIMEOUT 0

/** Open a RAW CAN socket and install the supplied explicit filters. */
int socketcan_raw_open_filtered(
    const char *ifname,
    const struct can_filter *filters,
    size_t filter_count);
/** Send one classic CAN frame. */
int socketcan_raw_send(int fd, uint32_t can_id, const uint8_t *data, size_t data_len);
/** Receive one frame; return 1 for a frame, 0 for timeout, or -1 for error. */
int socketcan_raw_recv(int fd, struct can_frame *frame_out, int timeout_ms);

#endif
