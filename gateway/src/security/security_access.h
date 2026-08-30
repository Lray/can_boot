#ifndef SECURITY_ACCESS_H
#define SECURITY_ACCESS_H

#include <stddef.h>
#include <stdint.h>

#include "token_signer_client.h"
#include "uds_client.h"

#define SECURITY_ERR_INVALID_ARG (-1101)
#define SECURITY_ERR_PROVIDER (-1102)
#define SECURITY_ERR_TOKEN_SIZE (-1103)

/** Request a programming seed, sign it through the signer client, and unlock
 *  the protected access level. */
int security_access_unlock(UdsClient *client, const TokenSignerClient_t *signer);

#endif
