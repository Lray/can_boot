if(NOT DEFINED GATEWAY_SOURCE_DIR)
    message(FATAL_ERROR "GATEWAY_SOURCE_DIR is required")
endif()

set(runtime_files
    "${GATEWAY_SOURCE_DIR}/config/systemd/mcu-updater.service"
    "${GATEWAY_SOURCE_DIR}/scripts/deploy_mcu_update_systemd_adb.ps1"
)
set(release_bound_pattern
    "MCU_UPDATE_|mcu-updater.conf|--work-root|--ifname|--endpoint|legacy_unit|[.]retired")

foreach(runtime_file IN LISTS runtime_files)
    file(READ "${runtime_file}" runtime_text)
    if(runtime_text MATCHES "${release_bound_pattern}")
        message(FATAL_ERROR "release-specific argument found in ${runtime_file}")
    endif()
endforeach()

set(state_files
    "${GATEWAY_SOURCE_DIR}/apps/mcu_updater/mcu_updater.c"
    "${GATEWAY_SOURCE_DIR}/config/systemd/mcu-updater.service"
    "${GATEWAY_SOURCE_DIR}/scripts/swupdate-restart"
    "${GATEWAY_SOURCE_DIR}/scripts/swupdate-suricatta"
)
foreach(state_file IN LISTS state_files)
    file(READ "${state_file}" state_text)
    if(state_text MATCHES "HAWKBIT_WAIT_FILE|confirmation/wait|mcu-update/confirmation|progress_ipc|CMD_ACTIVATION|-c 6")
        message(FATAL_ERROR "retired deferred-confirmation path found in ${state_file}")
    endif()
endforeach()

set(retired_files
    "${GATEWAY_SOURCE_DIR}/apps/lss_master/lss_master.c"
    "${GATEWAY_SOURCE_DIR}/apps/lss_master/lss_runtime.c"
    "${GATEWAY_SOURCE_DIR}/apps/lss_master/lss_runtime.h"
    "${GATEWAY_SOURCE_DIR}/adapters/swupdate/package_store.c"
    "${GATEWAY_SOURCE_DIR}/adapters/swupdate/package_store.h"
    "${GATEWAY_SOURCE_DIR}/adapters/swupdate/fs_util.c"
    "${GATEWAY_SOURCE_DIR}/adapters/swupdate/fs_util.h"
    "${GATEWAY_SOURCE_DIR}/adapters/swupdate/hawkbit_feedback.c"
    "${GATEWAY_SOURCE_DIR}/adapters/swupdate/hawkbit_feedback.h"
    "${GATEWAY_SOURCE_DIR}/cmake/swupdate-ipc.cmake"
    "${GATEWAY_SOURCE_DIR}/scripts/swupdate-enter-wait"
)
foreach(retired_file IN LISTS retired_files)
    if(EXISTS "${retired_file}")
        message(FATAL_ERROR "retired deferred-confirmation file exists: ${retired_file}")
    endif()
endforeach()

file(READ "${GATEWAY_SOURCE_DIR}/CMakeLists.txt" cmake_text)
if(cmake_text MATCHES "package_store|fs_util|hawkbit_feedback|swupdate-ipc|JSON_C|SWUPDATE_SOURCE_DIR|gateway-lss-runtime")
    message(FATAL_ERROR "retired mcu-updater dependency found")
endif()

file(READ "${GATEWAY_SOURCE_DIR}/apps/mcu_updater/update_job.c" job_text)
if(job_text MATCHES "CO_LSS|lss_|305/")
    message(FATAL_ERROR "OTA job must receive its Node-ID instead of performing LSS routing")
endif()

file(READ "${GATEWAY_SOURCE_DIR}/apps/mcu_updater/mcu_updater.c" updater_text)
if(updater_text MATCHES "CO_LSS|CO_CAN|305/|CO_driver")
    message(FATAL_ERROR "mcu-updater must request Node-ID over IPC, not own CANopen/LSS")
endif()
if(updater_text MATCHES "package_store|package-input-v1|/run/media/mmcblk0p6/mcu-update/packages")
    message(FATAL_ERROR "mcu-updater must not persist received packages")
endif()
if(NOT updater_text MATCHES "memfd_create")
    message(FATAL_ERROR "mcu-updater must keep the received image transaction-local")
endif()
string(FIND "${updater_text}"
    "RHmsg_send_ack(handler, update_rc == 0 ? \"ACK\" : \"NACK\")"
    result_reply_position)
if(result_reply_position EQUAL -1)
    message(FATAL_ERROR "final Remote Handler reply must reflect the MCU result")
endif()

# The production applications must have exactly one initialization owner.
file(GLOB_RECURSE app_sources "${GATEWAY_SOURCE_DIR}/apps/*.c")
set(can_initializations 0)
set(lss_initializations 0)
foreach(app_source IN LISTS app_sources)
    file(READ "${app_source}" app_text)
    string(REGEX MATCHALL "CO_CANmodule_init[(]" can_calls "${app_text}")
    string(REGEX MATCHALL "CO_LSSmaster_init[(]" lss_calls "${app_text}")
    list(LENGTH can_calls can_count)
    list(LENGTH lss_calls lss_count)
    math(EXPR can_initializations "${can_initializations} + ${can_count}")
    math(EXPR lss_initializations "${lss_initializations} + ${lss_count}")
endforeach()
if(NOT can_initializations EQUAL 1 OR NOT lss_initializations EQUAL 1)
    message(FATAL_ERROR "Only the resident gateway may initialize CANopen/LSS once")
endif()
file(READ "${GATEWAY_SOURCE_DIR}/apps/LSS_gateway/LSS_gateway.c" gateway_text)
if(gateway_text MATCHES "remote_handler|memfd|ota_|package_metadata|uds_client")
    message(FATAL_ERROR "LSS_gateway must not receive images or execute OTA")
endif()
foreach(gateway_call IN ITEMS CO_GTWA_init CO_GTWA_initRead CO_GTWA_write CO_GTWA_process)
    if(NOT gateway_text MATCHES "${gateway_call}[(]")
        message(FATAL_ERROR "LSS_gateway must use the official Gateway ASCII ${gateway_call} API")
    endif()
endforeach()
if(gateway_text MATCHES "CO_LSSmaster_IdentifyFastscan[(]|CO_GTWA_ST_LSS_ALLNODES|lss_allnodes|strtok|sscanf")
    message(FATAL_ERROR "LSS command parsing and Fastscan must remain in official CO_gateway_ascii")
endif()

file(READ "${GATEWAY_SOURCE_DIR}/scripts/swupdate-suricatta" suricatta_text)
if(NOT suricatta_text MATCHES "/usr/sbin/fw_printenv -n ustate")
    message(FATAL_ERROR "Suricatta launcher must read the official persistent state")
endif()
string(FIND "${suricatta_text}" "1) confirm='-c 2'" confirm_position)
if(confirm_position EQUAL -1)
    message(FATAL_ERROR "ustate=1 must report the completed synchronous update")
endif()
string(FIND "${suricatta_text}" "/usr/libexec/mcu-update/swupdate-restart" restart_position)
if(restart_position EQUAL -1)
    message(FATAL_ERROR "Suricatta must restart after a completed update")
endif()

file(READ "${GATEWAY_SOURCE_DIR}/config/systemd/mcu-updater.service" service_text)
if(NOT service_text MATCHES "Type=simple")
    message(FATAL_ERROR "mcu-updater must remain a resident service")
endif()
if(NOT service_text MATCHES "Restart=on-failure")
    message(FATAL_ERROR "mcu-updater must restart after a process failure")
endif()
if(service_text MATCHES "mcu-update/packages|ExecStartPre")
    message(FATAL_ERROR "mcu-updater service must not provision package storage")
endif()
string(FIND "${service_text}" "ExecStart=/run/media/mmcblk0p6/mcu-update/bin/mcu-updater" exec_position)
if(exec_position EQUAL -1)
    message(FATAL_ERROR "mcu-updater service must use the fixed production executable")
endif()
