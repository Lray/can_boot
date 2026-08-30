#ifndef SECURITY_ACCESS_H
#define SECURITY_ACCESS_H

#include <stdbool.h>
#include <stdint.h>

#include "shared/uds_protocol.h"

typedef enum
{
    SECURITY_ACCESS_RESULT_OK = 0,
    SECURITY_ACCESS_RESULT_INVALID_KEY,
    SECURITY_ACCESS_RESULT_EXCEEDED_ATTEMPTS,
    SECURITY_ACCESS_RESULT_DELAY_ACTIVE,
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
 * Advances SecurityAccess lockout state to now_ms.
 *
 * @param now_ms Monotonic diagnostic time in milliseconds.
 */
void SecurityAccess_Poll(uint32_t now_ms);

/**
 * Reports whether security access is currently unlocked.
 *
 * @return true when access remains unlocked; otherwise false.
 */
bool SecurityAccess_IsUnlocked(void);

/**
 * Generates the seed challenge for a programming SecurityAccess request.
 *
 * @param now_ms Monotonic diagnostic time in milliseconds.
 * @param seed Output buffer of SECURITY_ACCESS_SEED_SIZE bytes.
 * @return Request result, including active-delay and invalid-argument states.
 * @pre The security access module has been initialized and seed is valid.
 */
security_access_result_t SecurityAccess_RequestSeed(
    uint32_t now_ms,
    uint8_t seed[SECURITY_ACCESS_SEED_SIZE]);

/**
 * Validates a submitted programming token against the active seed challenge.
 *
 * @param key Token bytes to validate.
 * @param length Number of bytes in key.
 * @param now_ms Monotonic diagnostic time in milliseconds.
 * @return Validation, lockout, or sequence result.
 * @pre The security access module has been initialized and key is valid.
 */
security_access_result_t SecurityAccess_SubmitKey(
    const uint8_t *key,
    uint16_t length,
    uint32_t now_ms);

/**
 * Reports current lockout state without exposing mutable module state.
 *
 * @param now_ms Monotonic diagnostic time in milliseconds.
 * @param failed_attempts Optional output for the failed-attempt count.
 * @param remaining_delay_ms Optional output for the current lockout delay.
 * @pre The security access module has been initialized.
 */
void SecurityAccess_GetLockoutStatus(uint32_t now_ms,
                                         uint8_t *failed_attempts,
                                         uint32_t *remaining_delay_ms);

/* Board integration hook. Target builds must link the TRNG-backed
 * implementation; returning false keeps SecurityAccess fail-closed when
 * entropy is unavailable. */
bool SecurityAccess_GetEntropy(uint8_t *output, uint16_t length);

#endif /* SECURITY_ACCESS_H */
