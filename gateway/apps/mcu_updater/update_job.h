#ifndef MCU_UPDATE_JOB_H
#define MCU_UPDATE_JOB_H

#include <stdint.h>
#include "ota_executor.h"

#define MCU_UPDATE_EXIT_PACKAGE_POLICY 12
#define MCU_UPDATE_EXIT_SECURITY_ACCESS 20
#define MCU_UPDATE_EXIT_TRANSPORT 21
#define MCU_UPDATE_EXIT_EXECUTION 22
#define MCU_UPDATE_EXIT_TIMEOUT 23
#define MCU_UPDATE_EXIT_INTERNAL 28

typedef struct
{
    const char *can_ifname;
    uint8_t node_id;
    SecurityTokenTee_t *token_tee;
} McuUpdateConfig_t;

typedef struct
{
    OtaExecutorResult_t executor;
    OtaState_t terminal_state;
    int has_executor_result;
} McuUpdateResult_t;

/** The caller supplies the selected device's active Node-ID. */
int mcu_update_run_image(int image_fd, const McuUpdateConfig_t *config,
                         McuUpdateResult_t *result_out);

void mcu_update_log_result(const char *component, int exit_code,
                           const McuUpdateResult_t *result);

#endif
