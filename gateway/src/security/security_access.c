#include "security_access.h"

#include <stdio.h>

#include "profile.h"
#include "util.h"

/*
 * SID: 0x27 SecurityAccess - unlock the MCU's protected security level within
 * the current diagnostic session through a seed/token exchange.
 *
 * The exchange is 0x27 01 (request programming seed), followed by
 * 0x27 02 <token> (submit the signer-produced token).
 *
 * This function does not switch the diagnostic session. SID 0x10
 * DiagnosticSessionControl is responsible for selecting that session.
 */
int security_access_unlock(UdsClient *client, SecurityTokenTee_t *token_tee)
{
    uint8_t seed[SECURITY_ACCESS_SEED_MAX_SIZE] = {0};
    uint8_t token[SECURITY_ACCESS_TOKEN_MAX_SIZE] = {0};
    size_t seed_len = 0u;
    size_t token_len = 0u;
    int rc = 0;

    if (!uds_client_is_ready(client) || token_tee == NULL)
    {
        return SECURITY_ERR_INVALID_ARG;
    }
    rc = uds_security_request_seed(client, seed, sizeof(seed), &seed_len);
    if (rc == 0 && seed_len == SECURITY_ACCESS_SEED_SIZE)
    {
        bool already_unlocked = true;
        for (size_t index = 0u; index < seed_len; index++)
        {
            already_unlocked = already_unlocked && seed[index] == 0u;
        }
        if (already_unlocked)
        {
            secure_zero(seed, sizeof(seed));
            return 0;
        }
    }
    if (rc == 0)
    {
        const int tee_result = security_token_tee_issue(
            token_tee, seed, token, sizeof(token), &token_len);
        if (tee_result != 0)
        {
            fprintf(stderr, "mcu-update-engine: secure token issue failed rc=%d\n",
                    tee_result);
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
