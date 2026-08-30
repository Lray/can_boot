#ifndef SECURITY_CRYPTO_PORT_H
#define SECURITY_CRYPTO_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Compile-time trust anchor for the target, or the host fixture anchor. */
bool SecurityCrypto_GetProvisionedPublicKey(
    const uint8_t **key_out,
    size_t *key_length_out);

#endif /* SECURITY_CRYPTO_PORT_H */
