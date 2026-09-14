#include <stdio.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include "update_job.h"
#include "direct_args.h"

static int harden_process(void)
{
    struct rlimit no_core = {0u, 0u};

    (void)umask(0077u);
    return setrlimit(RLIMIT_CORE, &no_core) == 0 && prctl(PR_SET_DUMPABLE, 0) == 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    McuUpdaterDirectOptions_t options;
    McuUpdateConfig_t config = {0};
    McuUpdateResult_t result = {0};
    int terminal_rc;

    if (mcu_updater_direct_args_parse(argc, argv, &options) != 0 || harden_process() != 0)
    {
        fprintf(stderr, "mcu-updater-direct: invalid launch contract\n");
        return MCU_UPDATE_EXIT_INTERNAL;
    }
    config.can_ifname = options.ifname;
    config.signer_endpoint = options.signer_endpoint;
    config.signer_uid = options.signer_uid;
    config.signer_gid = options.signer_gid;
    config.signer_socket_gid = options.signer_socket_gid;
    config.signer_timeout_ms = options.signer_timeout_ms;

    terminal_rc = mcu_update_run_job(options.job_dir, &config, &result);
    mcu_update_log_result("mcu-updater-direct", terminal_rc, &result);
    return terminal_rc;
}
