#ifndef SHARED_SECURITY_TOKEN_PROFILE_H
#define SHARED_SECURITY_TOKEN_PROFILE_H

#include <stdint.h>

/*
 * Shared OTA-entry COSE_Sign1 CWT claim contract between the ECU firmware
 * (can/) and the Linux gateway (gateway/).  Single source of truth for the
 * token profile values; both trees include it and must not redefine these
 * macros locally.
 *
 * The token binds one fact: the signer authorized THIS seed challenge.  The
 * ECU verifies the ES256 signature against its provisioned public key and
 * compares both claims against its own outstanding seed; no other claims
 * carry decision content.
 *
 * Naming follows the ECU tree (UPPER_SNAKE with U suffix).
 */

#define SECURITY_TOKEN_MAX_SIZE 480U
#define SECURITY_TOKEN_SHA256_SIZE 32U
#define SECURITY_TOKEN_ES256_SIGNATURE_SIZE 64U

#define SECURITY_TOKEN_LABEL_SEED_CHALLENGE (-70001)
#define SECURITY_TOKEN_LABEL_FRESHNESS_NONCE (-70008)

#endif /* SHARED_SECURITY_TOKEN_PROFILE_H */
