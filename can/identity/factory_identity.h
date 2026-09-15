#ifndef FACTORY_IDENTITY_H
#define FACTORY_IDENTITY_H

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

/** Read the factory-programmed identity. False means unreadable or erased. */
bool FactoryIdentity_Read(factory_identity_t *identity);

#endif /* FACTORY_IDENTITY_H */
