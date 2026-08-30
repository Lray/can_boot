#ifndef TOKEN_SIGNER_TEE_H
#define TOKEN_SIGNER_TEE_H

#include <stddef.h>
#include <stdint.h>

typedef struct TokenSignerTee TokenSignerTee_t;

/*
 * Connect to the ECDSA-P256 signing TA in the OP-TEE secure world.
 * The TA loads the keypair from the keybox; the normal world only
 * receives the public key and signatures.
 *
 * Returns 0 on success, -1 on failure.
 */
int token_signer_tee_connect(TokenSignerTee_t **tee_out);

void token_signer_tee_disconnect(TokenSignerTee_t *tee);

/*
 * Read the TA's P-256 public key (x || y, 64 bytes). Returns 0 on
 * success, -1 on failure.
 */
int token_signer_tee_get_public_key(TokenSignerTee_t *tee,
                                    uint8_t public_key[64]);

/*
 * Sign a 32-byte SHA-256 digest with the TA's P-256 key and return the
 * raw r || s signature (64 bytes). Returns 0 on success, -1 on failure.
 */
int token_signer_tee_sign_digest(TokenSignerTee_t *tee,
                                 const uint8_t digest[32],
                                 uint8_t signature[64]);

#endif
