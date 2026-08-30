#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int (*send)(void *ctx, const uint8_t *data, size_t data_len);
    int (*recv)(void *ctx, uint8_t *data, size_t data_cap, size_t *data_len, uint32_t timeout_ms);
} TransportOps;

#endif
