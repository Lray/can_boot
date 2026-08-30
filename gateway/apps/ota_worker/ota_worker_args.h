#ifndef OTA_WORKER_ARGS_H
#define OTA_WORKER_ARGS_H

#include <stdint.h>
#include <sys/types.h>

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
} OtaWorkerOptions_t;

int ota_worker_args_parse_options(int argc, char **argv, OtaWorkerOptions_t *options);

#endif
