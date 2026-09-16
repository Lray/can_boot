#ifndef MCU_UPDATER_ARGS_H
#define MCU_UPDATER_ARGS_H

#define MCU_UPDATER_DEFAULT_CAN_IFNAME "awlink0"
typedef struct
{
    const char *work_root;
    const char *can_ifname;
    const char *endpoint_base;
    const char *signer_endpoint;
    const char *signer_uid;
    const char *signer_gid;
    const char *signer_socket_gid;
    const char *signer_timeout_ms;
} McuUpdaterOptions_t;

int mcu_updater_args_parse(int argc, char **argv, McuUpdaterOptions_t *options);
int mcu_updater_args_validate(const McuUpdaterOptions_t *options);
void mcu_updater_args_print_usage(const char *program);

#endif
