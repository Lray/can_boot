#ifndef SHARED_FACTORY_IDENTITY_H
#define SHARED_FACTORY_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t vendor_id;
    uint32_t product_code;
    uint32_t revision_number;
    uint32_t serial_number;
} factory_identity_t;

typedef char factory_identity_size_must_be_16_bytes[
    (sizeof(factory_identity_t) == 16U) ? 1 : -1];

static inline bool factory_identity_equal(const factory_identity_t *left,
                                          const factory_identity_t *right)
{
    return left->vendor_id == right->vendor_id &&
           left->product_code == right->product_code &&
           left->revision_number == right->revision_number &&
           left->serial_number == right->serial_number;
}

#endif /* SHARED_FACTORY_IDENTITY_H */
