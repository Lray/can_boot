#ifndef SECURITY_TOKEN_TEE_H
#define SECURITY_TOKEN_TEE_H

#include <stddef.h>
#include <stdint.h>

typedef struct SecurityTokenTee SecurityTokenTee_t;

int security_token_tee_open(SecurityTokenTee_t **tee_out);
void security_token_tee_close(SecurityTokenTee_t *tee);
int security_token_tee_issue(SecurityTokenTee_t *tee, const uint8_t seed[4],
                             uint8_t *token_out, size_t token_cap,
                             size_t *token_len_out);

#endif
