#include "security_access.h"

#include <stdio.h>

#include "profile.h"
#include "util.h"

/*
 * SID: 0x27 SecurityAccess - unlock the ECU's protected security level within
 * the current diagnostic session through a seed/token exchange.
 *
 * The exchange is 0x27 01 (request programming seed), followed by
 * 0x27 02 <token> (submit the signer-produced token).
 *
 * This function does not switch the diagnostic session. SID 0x10
 * DiagnosticSessionControl is responsible for selecting that session.
 */
int security_access_unlock(UdsClient *client, const TokenSignerClient_t *signer)
{
    uint8_t seed[SECURITY_ACCESS_SEED_MAX_SIZE] = {0};
    uint8_t token[SECURITY_ACCESS_TOKEN_MAX_SIZE] = {0};
    size_t seed_len = 0u;
    size_t token_len = 0u;
    int rc = 0;

    if (!uds_client_is_ready(client) || signer == NULL)
    {
        return SECURITY_ERR_INVALID_ARG;
    }
    rc = uds_security_request_seed(client, seed, sizeof(seed), &seed_len);
    if (rc == 0)
    {
        /*请求签名进程对种子挑战完成签名*/
        const int signer_rc = token_signer_client_issue(signer, seed, seed_len,
                                                        token, sizeof(token), &token_len);
        if (signer_rc != 0)
        {
            /* Numeric provider result only; never log seed or token. */
            fprintf(stderr, "gateway-worker: token-signer client failed rc=%d\n", signer_rc);
            rc = SECURITY_ERR_PROVIDER;
        }
    }
    if (rc == 0 && (token_len == 0u || token_len > sizeof(token)))
    {
        rc = SECURITY_ERR_TOKEN_SIZE;
    }
    if (rc == 0)
    {
        rc = uds_security_send_token(client, token, token_len);
    }

    secure_zero(seed, sizeof(seed));
    secure_zero(token, sizeof(token));
    return rc;
}
