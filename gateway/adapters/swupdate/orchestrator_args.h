#ifndef ORCHESTRATOR_ARGS_H
#define ORCHESTRATOR_ARGS_H

#include <stdint.h>

#define ORCHESTRATOR_DEFAULT_CAN_IFNAME "awlink0"
#define ORCHESTRATOR_DEFAULT_ENDPOINT "ipc:///run/ecu-ota/remote-handler/ecu-v1"
#define ORCHESTRATOR_WORKER_PATH "/run/media/mmcblk0p6/ecu-ota/bin/gateway-ota-worker-v1"
#define ORCHESTRATOR_WORKER_BASENAME "gateway-ota-worker-v1"

typedef struct
{
    const char *work_root;
    const char *job_id;
    const char *expected_size;
    const char *expected_sha256;
    const char *can_ifname;
    const char *endpoint;
    const char *signer_endpoint;
    const char *signer_uid;
    const char *signer_gid;
    const char *signer_socket_gid;
    const char *signer_timeout_ms;
} OrchestratorOptions_t;

int orchestrator_args_parse(int argc, char **argv, OrchestratorOptions_t *options);
int orchestrator_args_validate(const OrchestratorOptions_t *options, uint64_t *size_out,
                               uint8_t sha256_out[32]);
int orchestrator_args_open_worker(void);
void orchestrator_args_print_usage(const char *program);

#endif
