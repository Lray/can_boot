#ifndef MCU_UPDATER_DIRECT_ARGS_H
#define MCU_UPDATER_DIRECT_ARGS_H

#include <stdint.h>
#include <sys/types.h>
#include "shared/mcu_identity.h"

typedef struct
{
    const char *job_dir;
    const char *job_id;
    const char *ifname;
    const char *signer_endpoint;
    uid_t signer_uid;
    gid_t signer_gid;
    gid_t signer_socket_gid;
    uint32_t signer_timeout_ms;
    uint8_t target_identity[MCU_IDENTITY_SIZE];
} McuUpdaterDirectOptions_t;

int mcu_updater_direct_args_parse(int argc, char **argv, McuUpdaterDirectOptions_t *options);

#endif
