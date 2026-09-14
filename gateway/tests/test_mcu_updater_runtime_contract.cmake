if(NOT DEFINED GATEWAY_SOURCE_DIR)
    message(FATAL_ERROR "GATEWAY_SOURCE_DIR is required")
endif()

set(runtime_files
    "${GATEWAY_SOURCE_DIR}/config/systemd/mcu-updater.service"
    "${GATEWAY_SOURCE_DIR}/config/systemd/mcu-updater.conf.example"
    "${GATEWAY_SOURCE_DIR}/scripts/deploy_mcu_update_systemd_adb.ps1"
)
set(release_bound_pattern
    "MCU_UPDATE_JOB_ID|MCU_UPDATE_EXPECTED_SIZE|MCU_UPDATE_EXPECTED_SHA256|--job-id|--expected-size|--expected-sha256")

foreach(runtime_file IN LISTS runtime_files)
    file(READ "${runtime_file}" runtime_text)
    if(runtime_text MATCHES "${release_bound_pattern}")
        message(FATAL_ERROR "release-specific argument found in ${runtime_file}")
    endif()
endforeach()

file(READ "${GATEWAY_SOURCE_DIR}/config/systemd/mcu-updater.service" service_text)
if(NOT service_text MATCHES "Type=simple")
    message(FATAL_ERROR "mcu-updater must remain a resident service")
endif()
if(NOT service_text MATCHES "Restart=on-failure")
    message(FATAL_ERROR "mcu-updater must restart after a process failure")
endif()
string(FIND "${service_text}" "--work-root \${MCU_UPDATE_WORK_ROOT}" work_root_position)
if(work_root_position EQUAL -1)
    message(FATAL_ERROR "mcu-updater service must use the device-local work root")
endif()

file(READ "${GATEWAY_SOURCE_DIR}/config/systemd/mcu-updater.conf.example" config_text)
if(NOT config_text MATCHES "MCU_UPDATE_WORK_ROOT=/run/mcu-update/jobs")
    message(FATAL_ERROR "mcu-updater work root must use the systemd runtime directory")
endif()

file(READ "${GATEWAY_SOURCE_DIR}/scripts/build_mcu_hawkbit_swu_wsl.sh" builder_text)
if(builder_text MATCHES "hardware-compatibility|hardware_compatibility|hwrevision")
    message(FATAL_ERROR "the single-MCU SWU must not enable hardware compatibility fields")
endif()
string(FIND "${builder_text}" "type = \"remote\";" remote_type_position)
if(remote_type_position EQUAL -1)
    message(FATAL_ERROR "the SWU must contain one Remote Handler artifact")
endif()
string(FIND "${builder_text}" "data = \"mcu-v1\";" remote_endpoint_position)
if(remote_endpoint_position EQUAL -1)
    message(FATAL_ERROR "the SWU must identify the Remote Handler service")
endif()
string(FIND "${builder_text}" [=[sha256 = \"${image_sha256}\";]=] digest_position)
if(digest_position EQUAL -1)
    message(FATAL_ERROR "the signed SWU description must bind the artifact digest")
endif()
