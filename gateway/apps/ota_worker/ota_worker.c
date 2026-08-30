#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "isotp_channel.h"
#include "ota_executor.h"
#include "ota_worker_args.h"
#include "package_metadata.h"
#include "package_input.h"
#include "token_signer_client.h"
#include "uds_client.h"

#define WORKER_SIGNER_USER "ecu-token-signer"
#define WORKER_SIGNER_SOCKET_GROUP "ecu-token-client"

#define WORKER_EXIT_PACKAGE_POLICY 12
#define WORKER_EXIT_SECURITY_ACCESS 20
#define WORKER_EXIT_TRANSPORT 21
#define WORKER_EXIT_INTERNAL 28

typedef struct
{
    IsotpChannel *channel;
    IsotpChannelConfig config;
    const char *ifname;
} WorkerReconnectContext_t;

typedef struct
{
    OtaPackage_t package;
    OtaExecutorResult_t result;
    int has_executor_result;
} WorkerJobResult_t;

static int harden_process(void)
{
    struct rlimit no_core = {0u, 0u};

    (void)umask(0077u);
    return setrlimit(RLIMIT_CORE, &no_core) == 0 && prctl(PR_SET_DUMPABLE, 0) == 0 ? 0 : -1;
}

static int open_input_directory(const OtaWorkerOptions_t *options)
{
    struct stat job_stat;
    struct stat input_stat;
    int job_fd;
    int input_fd;

    job_fd = open(options->job_dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (job_fd < 0 || fstat(job_fd, &job_stat) != 0 || !S_ISDIR(job_stat.st_mode) ||
        job_stat.st_uid != geteuid() || (job_stat.st_mode & 0022u) != 0u)
    {
        if (job_fd >= 0)
        {
            (void)close(job_fd);
        }
        return -1;
    }
    input_fd = openat(job_fd, PACKAGE_INPUT_DIRECTORY,
                      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    (void)close(job_fd);
    if (input_fd < 0 || fstat(input_fd, &input_stat) != 0 || !S_ISDIR(input_stat.st_mode) ||
        input_stat.st_uid != geteuid() || (input_stat.st_mode & 0777u) != 0700u)
    {
        if (input_fd >= 0)
        {
            (void)close(input_fd);
        }
        return -1;
    }
    return input_fd;
}

static int fixed_member_path(int input_fd, const char *name, char *path_out, size_t path_cap)
{
    struct stat member_stat;
    int length;

    if (fstatat(input_fd, name, &member_stat, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISREG(member_stat.st_mode) || member_stat.st_nlink != 1 ||
        member_stat.st_uid != geteuid() || (member_stat.st_mode & 0777u) != 0400u)
    {
        return -1;
    }
    length = snprintf(path_out, path_cap, "/proc/self/fd/%d/%s", input_fd, name);
    return length > 0 && (size_t)length < path_cap ? 0 : -1;
}

static int reconnect_isotp(void *ctx, UdsClient *client)
{
    WorkerReconnectContext_t *reconnect = (WorkerReconnectContext_t *)ctx;

    isotp_channel_close(reconnect->channel);
    if (isotp_channel_open(reconnect->channel, reconnect->ifname, &reconnect->config) != 0)
    {
        return -1;
    }
    uds_client_init(client, isotp_channel_transport_ops(), reconnect->channel);
    return 0;
}

static int run_worker_ota(const OtaWorkerOptions_t *options, const char *image_path,
                          WorkerJobResult_t *result)
{
    TokenSignerClient_t signer = {0};
    OtaExecutorConfig_t executor_config = {0};
    WorkerReconnectContext_t reconnect;
    IsotpChannel channel = {-1};
    UdsClient client;
    int rc;

    rc = ota_package_load_validate(image_path, &result->package);
    if (rc != 0)
    {
        return WORKER_EXIT_PACKAGE_POLICY;
    }
    if (options->signer_endpoint == NULL ||
        options->signer_uid == 0u || options->signer_gid == 0u ||
        options->signer_socket_gid == 0u)
    {
        return WORKER_EXIT_SECURITY_ACCESS;
    }
    signer.endpoint = options->signer_endpoint;
    signer.expected_peer_uid = options->signer_uid;
    signer.expected_peer_gid = options->signer_gid;
    signer.expected_socket_uid = options->signer_uid;
    signer.expected_socket_gid = options->signer_socket_gid;
    signer.expected_socket_mode = 0660u;
    signer.timeout_ms = options->signer_timeout_ms != 0u
                                   ? options->signer_timeout_ms
                                   : TOKEN_SIGNER_DEFAULT_TIMEOUT_MS;
    isotp_channel_default_config(&reconnect.config);
    reconnect.channel = &channel;
    reconnect.ifname = options->ifname;
    if (isotp_channel_open(&channel, options->ifname, &reconnect.config) != 0)
    {
        return WORKER_EXIT_TRANSPORT;
    }
    uds_client_init(&client, isotp_channel_transport_ops(), &channel);
    executor_config.client = &client;
    executor_config.reconnect = reconnect_isotp;
    executor_config.reconnect_ctx = &reconnect;
    executor_config.signer = &signer;
    result->has_executor_result = 1;
    rc = ota_executor_run(&executor_config, &result->package, &result->result);
    isotp_channel_close(&channel);
    return rc;
}

static void report_result(OtaState_t state, const OtaExecutorResult_t *result)
{
    fprintf(stderr,
            "gateway-worker: outcome=%s state=%u "
            "before=%u/%u.%u.%u.%lu/%u after=%u/%u.%u.%u.%lu/%u\n",
            state == OTA_STATE_CONFIRMED ? "CONFIRMED" : "FAILED",
            (unsigned int)state,
            result->before.active_slot,
            (unsigned int)result->before.app_version.iv_major,
            (unsigned int)result->before.app_version.iv_minor,
            (unsigned int)result->before.app_version.iv_revision,
            (unsigned long)result->before.app_version.iv_build_num,
            result->before.confirm_result,
            result->after.active_slot,
            (unsigned int)result->after.app_version.iv_major,
            (unsigned int)result->after.app_version.iv_minor,
            (unsigned int)result->after.app_version.iv_revision,
            (unsigned long)result->after.app_version.iv_build_num,
            result->after.confirm_result);
}

int main(int argc, char **argv)
{
    OtaWorkerOptions_t options;
    WorkerJobResult_t result = {0};
    char image_path[PATH_MAX] = {0};
    int input_fd = -1;
    int terminal_rc = WORKER_EXIT_PACKAGE_POLICY;

    if (ota_worker_args_parse_options(argc, argv, &options) != 0 || harden_process() != 0)
    {
        fprintf(stderr, "gateway-worker: invalid launch contract\n");
        return WORKER_EXIT_INTERNAL;
    }
    input_fd = open_input_directory(&options);
    if (input_fd < 0 ||
        fixed_member_path(input_fd, PACKAGE_IMAGE_NAME, image_path, sizeof(image_path)) != 0)
    {
        fprintf(stderr, "gateway-worker: package storage rejected\n");
        if (input_fd >= 0)
        {
            (void)close(input_fd);
        }
        return terminal_rc;
    }
    terminal_rc = run_worker_ota(&options, image_path, &result);
    if (result.has_executor_result)
    {
        report_result((OtaState_t)terminal_rc, &result.result);
    }
    ota_package_release(&result.package);
    (void)close(input_fd);
    return terminal_rc;
}
