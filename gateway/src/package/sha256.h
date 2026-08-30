#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

#include "profile.h"

typedef struct {
    uint32_t state[8];
    uint64_t total_len;
    uint8_t buffer[64];
    size_t buffer_len;
} Sha256;

void sha256_init(Sha256 *ctx);
void sha256_update(Sha256 *ctx, const uint8_t *data, size_t len);
void sha256_final(Sha256 *ctx, uint8_t digest[PACKAGE_SHA256_SIZE]);
void sha256_compute(const uint8_t *data, size_t len, uint8_t digest[PACKAGE_SHA256_SIZE]);

#endif
