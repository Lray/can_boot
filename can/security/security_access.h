#ifndef SECURITY_ACCESS_H
#define SECURITY_ACCESS_H

#include <stdbool.h>
#include <stdint.h>

#include "shared/uds_protocol.h"

typedef enum
{
    SECURITY_ACCESS_RESULT_OK = 0,
    SECURITY_ACCESS_RESULT_INVALID_KEY,
    SECURITY_ACCESS_RESULT_SEQUENCE_ERROR,
    SECURITY_ACCESS_RESULT_INVALID_ARG,
    SECURITY_ACCESS_RESULT_ENTROPY_UNAVAILABLE,
} security_access_result_t;

/**
 * Initializes SecurityAccess state and the token-verification boundary.
 *
 * @pre Call before any other SecurityAccess_* function.
 */
void SecurityAccess_Init(void);

/**
 * Clears the active unlock and any outstanding seed challenge.
 *
 * @pre The security access module has been initialized.
 */
void SecurityAccess_ClearUnlock(void);

/**
 * Reports whether security access is currently unlocked.
 *
 * @return true when access remains unlocked; otherwise false.
 */
bool SecurityAccess_IsUnlocked(void);

/**
 * Generates the seed challenge for a programming SecurityAccess request.
 *
 * @param seed Output buffer of SECURITY_ACCESS_SEED_SIZE bytes.
 * @return Request result, including entropy and invalid-argument states.
 * @pre The security access module has been initialized and seed is valid.
 */
security_access_result_t SecurityAccess_RequestSeed(
    uint8_t seed[SECURITY_ACCESS_SEED_SIZE]);

/**
 * Validates a submitted programming token against the active seed challenge.
 *
 * @param key Token bytes to validate.
 * @param length Number of bytes in key.
 * @return Validation or sequence result. Brute-force delay is owned by the
 *         upstream UDS server state machine.
 * @pre The security access module has been initialized and key is valid.
 */
security_access_result_t SecurityAccess_SubmitKey(
    const uint8_t *key,
    uint16_t length);

/* Board integration hook. Target builds must link the TRNG-backed
 * implementation; returning false keeps SecurityAccess fail-closed when
 * entropy is unavailable. */
bool SecurityAccess_GetEntropy(uint8_t *output, uint16_t length);

#endif /* SECURITY_ACCESS_H */
