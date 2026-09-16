#include "update_job.h"

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "isotp_channel.h"
#include "package_input.h"
#include "package_metadata.h"
#include "profile.h"
#include "token_signer_client.h"
#include "uds_client.h"
#include "util.h"
#include "mcu_registry.h"
#include "byte_order.h"

typedef struct
{
    IsotpChannel *channel;
    IsotpChannelConfig config;
    const char *ifname;
    uint64_t operation_deadline_ms;
} McuReconnectContext_t;

typedef struct
{
    timer_t timer;
    struct sigaction previous_action;
    int timer_created;
    int handler_installed;
} McuUpdateWatchdog_t;

static void update_watchdog_expired(int signal_number)
{
    static const char message[] =
        "mcu-updater: outcome=FAILED code=23 reason=total-timeout\n";

    (void)signal_number;
    (void)write(STDERR_FILENO, message, sizeof(message) - 1u);
    _exit(MCU_UPDATE_EXIT_TIMEOUT);
}

static int arm_update_watchdog(McuUpdateWatchdog_t *watchdog,
                               uint64_t *deadline_ms_out)
{
    struct sigaction action;
    struct sigevent event;
    struct itimerspec timeout;

    if (watchdog == NULL || deadline_ms_out == NULL)
    {
        return -1;
    }
    memset(watchdog, 0, sizeof(*watchdog));
    memset(&action, 0, sizeof(action));
    action.sa_handler = update_watchdog_expired;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGALRM, &action, &watchdog->previous_action) != 0)
    {
        return -1;
    }
    watchdog->handler_installed = 1;
    memset(&event, 0, sizeof(event));
    event.sigev_notify = SIGEV_SIGNAL;
    event.sigev_signo = SIGALRM;
    if (timer_create(CLOCK_MONOTONIC, &event, &watchdog->timer) != 0)
    {
        (void)sigaction(SIGALRM, &watchdog->previous_action, NULL);
        watchdog->handler_installed = 0;
        return -1;
    }
    watchdog->timer_created = 1;
    memset(&timeout, 0, sizeof(timeout));
    timeout.it_value.tv_sec = (time_t)(MCU_UPDATE_TOTAL_TIMEOUT_MS / 1000u);
    timeout.it_value.tv_nsec =
        (long)((MCU_UPDATE_TOTAL_TIMEOUT_MS % 1000u) * 1000000u);
    if (timer_settime(watchdog->timer, 0, &timeout, NULL) != 0)
    {
        (void)timer_delete(watchdog->timer);
        (void)sigaction(SIGALRM, &watchdog->previous_action, NULL);
        memset(watchdog, 0, sizeof(*watchdog));
        return -1;
    }
    *deadline_ms_out = util_monotonic_ms() + MCU_UPDATE_TOTAL_TIMEOUT_MS;
    return 0;
}

static void disarm_update_watchdog(McuUpdateWatchdog_t *watchdog)
{
    if (watchdog == NULL)
    {
        return;
    }
    if (watchdog->timer_created)
    {
        (void)timer_delete(watchdog->timer);
    }
    if (watchdog->handler_installed)
    {
        (void)sigaction(SIGALRM, &watchdog->previous_action, NULL);
    }
    memset(watchdog, 0, sizeof(*watchdog));
}

static int open_input_directory(const char *job_dir)
{
    struct stat job_stat;
    struct stat input_stat;
    int job_fd;
    int input_fd;

    job_fd = open(job_dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
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

static int fixed_image_path(int input_fd, char *path_out, size_t path_cap)
{
    struct stat image_stat;
    int length;

    if (fstatat(input_fd, PACKAGE_IMAGE_NAME, &image_stat, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISREG(image_stat.st_mode) || image_stat.st_nlink != 1 ||
        image_stat.st_uid != geteuid() || (image_stat.st_mode & 0777u) != 0400u)
    {
        return -1;
    }
    length = snprintf(path_out, path_cap, "/proc/self/fd/%d/%s", input_fd,
                      PACKAGE_IMAGE_NAME);
    return length > 0 && (size_t)length < path_cap ? 0 : -1;
}

static int reconnect_isotp(void *ctx, UdsClient *client)
{
    McuReconnectContext_t *reconnect = (McuReconnectContext_t *)ctx;

    isotp_channel_close(reconnect->channel);
    if (isotp_channel_open(reconnect->channel, reconnect->ifname, &reconnect->config) != 0)
    {
        return -1;
    }
    uds_client_init(client, isotp_channel_transport_ops(), reconnect->channel);
    uds_client_set_operation_deadline(client, reconnect->operation_deadline_ms);
    return 0;
}

int mcu_update_run_job(const char *job_dir, const McuUpdateConfig_t *config,
                       McuUpdateResult_t *result_out)
{
    TokenSignerClient_t signer = {0};
    OtaExecutorConfig_t executor_config = {0};
    McuReconnectContext_t reconnect;
    OtaPackage_t package = {0};
    IsotpChannel channel = {-1};
    UdsClient client;
    McuUpdateWatchdog_t watchdog;
    char image_path[PATH_MAX] = {0};
    int input_fd = -1;
    int rc = MCU_UPDATE_EXIT_PACKAGE_POLICY;
    McuRegistry registry = {.lock_fd = -1};
    CO_LSS_address_t identity;
    uint8_t node_id;

    if (job_dir == NULL || config == NULL || result_out == NULL ||
        config->can_ifname == NULL || config->signer_endpoint == NULL ||
        config->target_identity == NULL)
    {
        return MCU_UPDATE_EXIT_INTERNAL;
    }
    memset(result_out, 0, sizeof(*result_out));
    if (arm_update_watchdog(&watchdog, &reconnect.operation_deadline_ms) != 0)
    {
        return MCU_UPDATE_EXIT_INTERNAL;
    }
    input_fd = open_input_directory(job_dir);
    if (input_fd < 0 || fixed_image_path(input_fd, image_path, sizeof(image_path)) != 0 ||
        ota_package_load_validate(image_path, &package) != 0)
    {
        goto out;
    }
    for (size_t i = 0; i < 4; ++i)
    {
        identity.addr[i] = byte_order_get_u32_be(config->target_identity + i * 4);
    }
    if (mcu_registry_open(&registry, config->can_ifname, false) != 0 ||
        mcu_registry_lookup(&registry, &identity, &node_id) != 0)
    {
        fprintf(stderr, "mcu-updater: target not registered, registry invalid or bus busy\n");
        goto out;
    }
    fprintf(stderr,
            "mcu-updater: target=%08" PRIX32 ":%08" PRIX32 ":%08" PRIX32 ":%08" PRIX32
            " node-id=%u\n",
            identity.identity.vendorID, identity.identity.productCode,
            identity.identity.revisionNumber, identity.identity.serialNumber,
            (unsigned int)node_id);
    if (config->signer_uid == 0u || config->signer_gid == 0u ||
        config->signer_socket_gid == 0u)
    {
        rc = MCU_UPDATE_EXIT_SECURITY_ACCESS;
        goto out;
    }
    signer.endpoint = config->signer_endpoint;
    signer.expected_peer_uid = config->signer_uid;
    signer.expected_peer_gid = config->signer_gid;
    signer.expected_socket_uid = config->signer_uid;
    signer.expected_socket_gid = config->signer_socket_gid;
    signer.expected_socket_mode = 0660u;
    signer.timeout_ms = config->signer_timeout_ms != 0u
                            ? config->signer_timeout_ms
                            : TOKEN_SIGNER_DEFAULT_TIMEOUT_MS;

    reconnect.config = (IsotpChannelConfig){
        .request_id = CAN_ID_UDS_REQUEST(node_id),
        .response_id = CAN_ID_UDS_RESPONSE(node_id),
        .block_size = ISOTP_BLOCK_SIZE, .stmin_raw = ISOTP_STMIN_MS};
    reconnect.channel = &channel;
    reconnect.ifname = config->can_ifname;
    if (isotp_channel_open(&channel, config->can_ifname, &reconnect.config) != 0)
    {
        rc = MCU_UPDATE_EXIT_TRANSPORT;
        goto out;
    }
    uds_client_init(&client, isotp_channel_transport_ops(), &channel);
    uds_client_set_operation_deadline(&client, reconnect.operation_deadline_ms);
    executor_config.client = &client;
    executor_config.reconnect = reconnect_isotp;
    executor_config.reconnect_ctx = &reconnect;
    executor_config.signer = &signer;
    executor_config.expected_identity = config->target_identity;
    result_out->has_executor_result = 1;
    result_out->terminal_state = ota_executor_run(&executor_config, &package,
                                                   &result_out->executor);
    rc = result_out->terminal_state == OTA_STATE_CONFIRMED
             ? 0
             : MCU_UPDATE_EXIT_EXECUTION;

out:
    mcu_registry_close(&registry);
    disarm_update_watchdog(&watchdog);
    isotp_channel_close(&channel);
    ota_package_release(&package);
    if (input_fd >= 0)
    {
        (void)close(input_fd);
    }
    return rc;
}

void mcu_update_log_result(const char *component, int exit_code,
                           const McuUpdateResult_t *result)
{
    const char *name = component != NULL ? component : "mcu-updater";

    if (result == NULL || !result->has_executor_result)
    {
        fprintf(stderr, "%s: outcome=FAILED code=%d\n", name, exit_code);
        return;
    }
    fprintf(stderr,
            "%s: outcome=%s state=%u "
            "before=%u/%u.%u.%u.%lu/%u after=%u/%u.%u.%u.%lu/%u\n",
            name, result->terminal_state == OTA_STATE_CONFIRMED ? "CONFIRMED" : "FAILED",
            (unsigned int)result->terminal_state,
            result->executor.before.active_slot,
            (unsigned int)result->executor.before.app_version.iv_major,
            (unsigned int)result->executor.before.app_version.iv_minor,
            (unsigned int)result->executor.before.app_version.iv_revision,
            (unsigned long)result->executor.before.app_version.iv_build_num,
            result->executor.before.confirm_result,
            result->executor.after.active_slot,
            (unsigned int)result->executor.after.app_version.iv_major,
            (unsigned int)result->executor.after.app_version.iv_minor,
            (unsigned int)result->executor.after.app_version.iv_revision,
            (unsigned long)result->executor.after.app_version.iv_build_num,
            result->executor.after.confirm_result);
}
