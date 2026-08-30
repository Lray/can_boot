#ifndef TOKEN_SIGNER_DAEMON_H
#define TOKEN_SIGNER_DAEMON_H

#include <stdint.h>
#include <sys/types.h>

#include "token_signer_tee.h"

#define TOKEN_SIGNER_DAEMON_DEFAULT_ENDPOINT "/run/ecu-token-signer/v1.sock"

typedef struct
{
    uid_t signer_uid;
    gid_t signer_gid;
    gid_t socket_gid;
    uid_t client_uid;
    gid_t client_gid;
} TokenSignerDaemonPolicy_t;

int token_signer_daemon_run(const char *endpoint, TokenSignerTee_t *tee,
                            unsigned int idle_timeout_s,
                            const TokenSignerDaemonPolicy_t *policy);

#endif
