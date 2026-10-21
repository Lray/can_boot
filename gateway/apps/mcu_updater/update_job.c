#include "update_job.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "isotp_channel.h"
#include "package_metadata.h"
#include "profile.h"
#include "uds_client.h"
#include "util.h"

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

int mcu_update_run_image(int image_fd, const McuUpdateConfig_t *config,
                         McuUpdateResult_t *result_out)
{
    OtaExecutorConfig_t executor_config = {0};
    McuReconnectContext_t reconnect;
    OtaPackage_t package = {0};
    IsotpChannel channel = {-1};
    UdsClient client;
    McuUpdateWatchdog_t watchdog;
    int rc = MCU_UPDATE_EXIT_PACKAGE_POLICY;

    if (image_fd < 0 || config == NULL || result_out == NULL ||
        config->can_ifname == NULL || config->token_tee == NULL ||
        config->node_id < CAN_NODE_ID_MIN || config->node_id > CAN_NODE_ID_MAX)
    {
        return MCU_UPDATE_EXIT_INTERNAL;
    }
    memset(result_out, 0, sizeof(*result_out));
    if (arm_update_watchdog(&watchdog, &reconnect.operation_deadline_ms) != 0)
    {
        return MCU_UPDATE_EXIT_INTERNAL;
    }
    if (ota_package_load_validate(image_fd, &package) != 0)
    {
        goto out;
    }
    reconnect.config = (IsotpChannelConfig){
        .request_id = CAN_ID_UDS_REQUEST(config->node_id),
        .response_id = CAN_ID_UDS_RESPONSE(config->node_id),
        .block_size = ISOTP_BLOCK_SIZE,
        .stmin_raw = ISOTP_STMIN_MS,
    };
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
    executor_config.token_tee = config->token_tee;
    result_out->has_executor_result = 1;
    result_out->terminal_state = ota_executor_run(&executor_config, &package,
                                                   &result_out->executor);
    rc = result_out->terminal_state == OTA_STATE_CONFIRMED
             ? 0
             : MCU_UPDATE_EXIT_EXECUTION;

out:
    disarm_update_watchdog(&watchdog);
    isotp_channel_close(&channel);
    ota_package_release(&package);
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
