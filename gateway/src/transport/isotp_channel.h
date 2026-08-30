#ifndef ISOTP_CHANNEL_H
#define ISOTP_CHANNEL_H

#include <stdint.h>

#include "transport.h"

typedef struct {
    uint32_t request_id;
    uint32_t response_id;
    uint8_t block_size;
    /* Raw ISO-TP STmin encoding: 0x00..0x7F ms or 0xF1..0xF9 in 100 us. */
    uint8_t stmin_raw;
} IsotpChannelConfig;

typedef struct {
    int fd;
} IsotpChannel;

void isotp_channel_default_config(IsotpChannelConfig *config);
int isotp_channel_open(IsotpChannel *channel, const char *ifname, const IsotpChannelConfig *config);
void isotp_channel_close(IsotpChannel *channel);
const TransportOps *isotp_channel_transport_ops(void);

#endif
