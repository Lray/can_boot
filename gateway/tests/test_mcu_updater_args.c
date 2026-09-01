#include <assert.h>
#include <string.h>

#include "mcu_updater_args.h"
#include "remote_handler.h"

static void test_accepts_device_configuration(void)
{
    char *argv[] = {
        "mcu-updater",
        "--work-root", "/run/mcu-update/jobs",
        "--ifname", "awlink0",
        "--endpoint", "ipc:///run/mcu-update/remote-handler/mcu-v1",
        "--signer-endpoint", "/run/mcu-token-signer/v1.sock",
        "--signer-uid", "200",
        "--signer-gid", "200",
        "--signer-socket-gid", "201",
        "--signer-timeout-ms", "1000",
    };
    McuUpdaterOptions_t options;

    assert(mcu_updater_args_parse((int)(sizeof(argv) / sizeof(argv[0])), argv, &options) == 0);
    assert(mcu_updater_args_validate(&options) == 0);
    assert(strcmp(options.work_root, "/run/mcu-update/jobs") == 0);
    assert(strcmp(options.can_ifname, "awlink0") == 0);
    assert(strcmp(options.endpoint, REMOTE_HANDLER_DEFAULT_ENDPOINT) == 0);
}

static void test_defaults_are_device_configuration(void)
{
    char *argv[] = {
        "mcu-updater",
        "--work-root", "/run/mcu-update/jobs",
        "--signer-endpoint", "/run/mcu-token-signer/v1.sock",
        "--signer-uid", "200",
        "--signer-gid", "200",
        "--signer-socket-gid", "201",
        "--signer-timeout-ms", "250",
    };
    McuUpdaterOptions_t options;

    assert(mcu_updater_args_parse((int)(sizeof(argv) / sizeof(argv[0])), argv, &options) == 0);
    assert(mcu_updater_args_validate(&options) == 0);
    assert(strcmp(options.can_ifname, MCU_UPDATER_DEFAULT_CAN_IFNAME) == 0);
    assert(strcmp(options.endpoint, REMOTE_HANDLER_DEFAULT_ENDPOINT) == 0);
}

static void test_rejects_release_arguments(void)
{
    static const char *removed_names[] = {
        "--job-id",
        "--expected-size",
        "--expected-sha256",
    };
    size_t index;

    for (index = 0u; index < sizeof(removed_names) / sizeof(removed_names[0]); ++index)
    {
        char *argv[] = {"mcu-updater", (char *)removed_names[index], "value"};
        McuUpdaterOptions_t options;

        assert(mcu_updater_args_parse(3, argv, &options) != 0);
    }
}

static void test_rejects_duplicate_or_incomplete_configuration(void)
{
    char *duplicate[] = {
        "mcu-updater",
        "--work-root", "/run/mcu-update/jobs",
        "--work-root", "/run/mcu-update/other",
    };
    char *missing_value[] = {"mcu-updater", "--work-root"};
    McuUpdaterOptions_t options;

    assert(mcu_updater_args_parse((int)(sizeof(duplicate) / sizeof(duplicate[0])), duplicate,
                                  &options) != 0);
    assert(mcu_updater_args_parse((int)(sizeof(missing_value) / sizeof(missing_value[0])),
                                  missing_value, &options) != 0);
}

int main(void)
{
    test_accepts_device_configuration();
    test_defaults_are_device_configuration();
    test_rejects_release_arguments();
    test_rejects_duplicate_or_incomplete_configuration();
    return 0;
}
