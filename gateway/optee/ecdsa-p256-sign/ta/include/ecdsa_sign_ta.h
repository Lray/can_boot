/*
 * ecdsa_sign_ta.h
 *
 * Shared contract between the ECDSA-P256 signing TA and its client
 * (token-signer-daemon). The UUID and command IDs must stay in sync
 * across the secure world (TA) and the normal world (TEEC client).
 */

#ifndef ECDSA_SIGN_TA_H
#define ECDSA_SIGN_TA_H

#define ECDSA_SIGN_TA_UUID \
    { 0x724b12aa, 0x6e74, 0x4779, \
      { 0xbf, 0x3a, 0x15, 0x80, 0xa0, 0x76, 0xfe, 0xd3 } }

#define ECDSA_SIGN_CMD_GET_PUBLIC_KEY 1u
#define ECDSA_SIGN_CMD_SIGN_DIGEST 2u

#define ECDSA_P256_PRIVATE_KEY_SIZE 32u
#define ECDSA_P256_PUBLIC_KEY_SIZE 64u
#define ECDSA_P256_SIGNATURE_SIZE 64u
#define ECDSA_P256_DIGEST_SIZE 32u

#endif
