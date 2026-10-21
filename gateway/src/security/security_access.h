#ifndef SECURITY_ACCESS_H
#define SECURITY_ACCESS_H

#include <stddef.h>
#include <stdint.h>

#include "security_token_tee.h"
#include "uds_client.h"

#define SECURITY_ERR_INVALID_ARG (-1101)
#define SECURITY_ERR_PROVIDER (-1102)
#define SECURITY_ERR_TOKEN_SIZE (-1103)

/** Request a programming seed, issue its protocol-bound token in secure world,
 *  and unlock the protected access level. */
int security_access_unlock(UdsClient *client, SecurityTokenTee_t *token_tee);

#endif
