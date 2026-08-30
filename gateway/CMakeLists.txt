cmake_minimum_required(VERSION 3.16)
project(gateway_ota C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BUILD_TESTING "Build host-only Gateway tests" OFF)
if(BUILD_TESTING)
    enable_testing()
endif()

set(GATEWAY_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codec
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport
    ${CMAKE_CURRENT_SOURCE_DIR}/src/uds
    ${CMAKE_CURRENT_SOURCE_DIR}/src/security
    ${CMAKE_CURRENT_SOURCE_DIR}/src/package
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ota
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_library(gateway-byte-order STATIC
    src/codec/byte_order.c
)
target_include_directories(gateway-byte-order PUBLIC
    ${GATEWAY_INCLUDE_DIRS}
)
target_compile_definitions(gateway-byte-order PUBLIC _DEFAULT_SOURCE)

add_library(gateway-hash STATIC
    src/package/sha256.c
)
target_include_directories(gateway-hash PUBLIC
    ${GATEWAY_INCLUDE_DIRS}
)
target_link_libraries(gateway-hash PUBLIC gateway-byte-order)

add_library(gateway-core STATIC
    src/util/util.c
    src/ota/resume_transfer.c
    src/ota/ota_snapshot.c
    src/ota/ota_executor.c
    src/package/package_metadata.c
    src/security/security_access.c
    src/uds/uds_client.c
    src/uds/uds_transaction.c
    src/transport/isotp_channel.c
)

target_include_directories(gateway-core PUBLIC
    ${GATEWAY_INCLUDE_DIRS}
)

set(LINUX_UAPI_INCLUDE_DIR "" CACHE PATH
    "Linux UAPI include directory containing linux/can/isotp.h")
if(LINUX_UAPI_INCLUDE_DIR)
    if(NOT EXISTS "${LINUX_UAPI_INCLUDE_DIR}/linux/can/isotp.h")
        message(FATAL_ERROR
            "LINUX_UAPI_INCLUDE_DIR must contain linux/can/isotp.h")
    endif()
    target_include_directories(gateway-core SYSTEM PRIVATE
        ${LINUX_UAPI_INCLUDE_DIR}
    )
endif()

target_compile_definitions(gateway-core PUBLIC _DEFAULT_SOURCE)

find_package(Threads REQUIRED)
target_link_libraries(gateway-core PUBLIC
    gateway-byte-order
    gateway-hash
    Threads::Threads
    pthread
)

add_library(gateway-socketcan STATIC
    src/transport/socketcan_raw.c
)
target_include_directories(gateway-socketcan PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_definitions(gateway-socketcan PUBLIC _DEFAULT_SOURCE)

add_library(gateway-log-reassembly STATIC
    src/log/log_frame.c
    src/log/log_receiver.c
)
target_include_directories(gateway-log-reassembly PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/log
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_library(gateway-log-sink STATIC
    src/log/log_sink.c
)
target_include_directories(gateway-log-sink PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/log
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_library(gateway-bridge-support STATIC
    adapters/swupdate/package_store.c
    adapters/swupdate/fs_util.c
)
target_include_directories(gateway-bridge-support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/swupdate
    ${GATEWAY_INCLUDE_DIRS}
)
target_compile_definitions(gateway-bridge-support PUBLIC _GNU_SOURCE)
target_link_libraries(gateway-bridge-support PUBLIC gateway-hash)

set(GATEWAY_QCBOR_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/QCBOR)
add_library(gateway-qcbor STATIC
    ${GATEWAY_QCBOR_ROOT}/src/qcbor_decode.c
    ${GATEWAY_QCBOR_ROOT}/src/qcbor_encode.c
    ${GATEWAY_QCBOR_ROOT}/src/ieee754.c
    ${GATEWAY_QCBOR_ROOT}/src/UsefulBuf.c
)
target_include_directories(gateway-qcbor PUBLIC ${GATEWAY_QCBOR_ROOT}/inc)
target_compile_definitions(gateway-qcbor PRIVATE QCBOR_DISABLE_FLOAT_HW_USE)

add_library(gateway-token-signer-client STATIC
    src/security/token_signer_codec.c
    src/security/token_signer_client.c
)
target_include_directories(gateway-token-signer-client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/security
    ${GATEWAY_INCLUDE_DIRS}
)
target_compile_definitions(gateway-token-signer-client PRIVATE _GNU_SOURCE)
target_link_libraries(gateway-token-signer-client PRIVATE gateway-core gateway-qcbor)

add_executable(gateway-ota-worker-v1
    apps/ota_worker/ota_worker.c
    apps/ota_worker/ota_worker_args.c
)
target_include_directories(gateway-ota-worker-v1 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/ota_worker
)
target_compile_definitions(gateway-ota-worker-v1 PRIVATE _GNU_SOURCE)
target_link_libraries(gateway-ota-worker-v1 PRIVATE
    gateway-core
    gateway-token-signer-client
)

add_executable(gateway-ota-package-probe
    apps/ota_worker/ota_package_probe.c
)
target_include_directories(gateway-ota-package-probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/ota_worker
)
target_link_libraries(gateway-ota-package-probe PRIVATE gateway-core)

add_executable(gateway-log-receiver
    apps/log_receiver/log_receiver_main.c
)
target_link_libraries(gateway-log-receiver PRIVATE
    gateway-log-reassembly
    gateway-log-sink
    gateway-socketcan
)

option(ENABLE_SWUPDATE_BRIDGE
       "Build the SWUpdate remote handler orchestrator" OFF)
if(ENABLE_SWUPDATE_BRIDGE)
    find_path(ZMQ_INCLUDE_DIR zmq.h)
    find_library(ZMQ_LIBRARY NAMES zmq)
    if(NOT ZMQ_INCLUDE_DIR OR NOT ZMQ_LIBRARY)
        message(FATAL_ERROR "ENABLE_SWUPDATE_BRIDGE requires libzmq headers and library")
    endif()

    add_library(gateway-remote-handler STATIC
        adapters/swupdate/remote_handler.c
    )
    target_include_directories(gateway-remote-handler PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/adapters/swupdate
        ${ZMQ_INCLUDE_DIR}
    )
    target_compile_definitions(gateway-remote-handler PRIVATE _GNU_SOURCE)
    target_link_libraries(gateway-remote-handler PUBLIC ${ZMQ_LIBRARY})

    add_executable(ecu-ota-orchestrator
        adapters/swupdate/orchestrator.c
        adapters/swupdate/orchestrator_args.c
    )
    target_include_directories(ecu-ota-orchestrator SYSTEM PRIVATE ${ZMQ_INCLUDE_DIR})
    target_compile_definitions(ecu-ota-orchestrator PRIVATE _GNU_SOURCE)
    target_link_libraries(ecu-ota-orchestrator PRIVATE
        gateway-bridge-support
        gateway-remote-handler
        gateway-core
        ${ZMQ_LIBRARY}
    )

endif()

if(BUILD_TESTING)
add_executable(test-gateway-byte-order
    tests/test_byte_order.c
)
target_link_libraries(test-gateway-byte-order PRIVATE gateway-core)
add_test(NAME gateway-byte-order COMMAND test-gateway-byte-order)

add_executable(test-gateway-util
    tests/test_util.c
)
target_link_libraries(test-gateway-util PRIVATE gateway-core)
add_test(NAME gateway-util COMMAND test-gateway-util)


add_executable(test-profile
    tests/test_profile.c
)
target_link_libraries(test-profile PRIVATE gateway-core)
add_test(NAME profile COMMAND test-profile)

add_executable(test-isotp-channel
    tests/test_isotp_channel.c
)
target_link_libraries(test-isotp-channel PRIVATE gateway-core)
add_test(NAME isotp-channel COMMAND test-isotp-channel)

add_executable(test-gateway-uds-client
    tests/test_uds_client.c
)
target_link_libraries(test-gateway-uds-client PRIVATE gateway-core)
add_test(NAME gateway-uds-client COMMAND test-gateway-uds-client)

add_executable(test-gateway-log-receiver
    tests/test_log_receiver.c
    src/log/log_frame.c
    src/log/log_receiver.c
)
target_include_directories(test-gateway-log-receiver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/log
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME gateway-log-receiver COMMAND test-gateway-log-receiver)

add_executable(test-gateway-resume-transfer
    tests/test_resume_transfer.c
)
target_link_libraries(test-gateway-resume-transfer PRIVATE gateway-core)
add_test(NAME gateway-resume-transfer COMMAND test-gateway-resume-transfer)

add_executable(test-gateway-package-store
    tests/test_package_store.c
)
target_link_libraries(test-gateway-package-store PRIVATE gateway-bridge-support)
add_test(NAME gateway-package-store COMMAND test-gateway-package-store)
add_executable(test-gateway-package-metadata
    tests/test_package_metadata.c
)
target_compile_definitions(test-gateway-package-metadata PRIVATE
    TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(test-gateway-package-metadata PRIVATE gateway-core)
add_test(NAME gateway-package-metadata COMMAND test-gateway-package-metadata)

add_executable(test-gateway-token-signer-client
    tests/test_token_signer_client.c
    tests/fake_signer.c
)
target_compile_definitions(test-gateway-token-signer-client PRIVATE _GNU_SOURCE)
target_link_libraries(test-gateway-token-signer-client PRIVATE
    gateway-core
    gateway-token-signer-client
    gateway-qcbor
    Threads::Threads
)
add_test(NAME gateway-token-signer-client COMMAND test-gateway-token-signer-client)

add_executable(test-gateway-security-access
    tests/test_security_access.c
    tests/fake_signer.c
)
target_link_libraries(test-gateway-security-access PRIVATE
    gateway-core
    gateway-token-signer-client
    gateway-qcbor
    Threads::Threads
)
add_test(NAME gateway-security-access COMMAND test-gateway-security-access)

endif()

add_executable(raw_can_smoke
    tools/smoke/raw_can_smoke.c
)
target_link_libraries(raw_can_smoke PRIVATE gateway-socketcan)

add_executable(uds_smoke
    tools/smoke/uds_smoke.c
)
target_link_libraries(uds_smoke PRIVATE gateway-core)

add_executable(uds_security_smoke
    tools/smoke/uds_security_smoke.c
)
target_compile_definitions(uds_security_smoke PRIVATE _GNU_SOURCE)
target_link_libraries(uds_security_smoke PRIVATE
    gateway-core
    gateway-token-signer-client
)

option(ENABLE_TOKEN_SIGNER_DAEMON
       "Build the C token signer daemon" OFF)
if(ENABLE_TOKEN_SIGNER_DAEMON)
    find_package(OpenSSL REQUIRED)

    set(TEE_INCLUDE_DIR "" CACHE PATH "OP-TEE export-ca include directory")
    set(TEE_LIBRARY "" CACHE FILEPATH "libteec path")
    if(NOT TEE_INCLUDE_DIR OR NOT TEE_LIBRARY)
        message(FATAL_ERROR "TEE_INCLUDE_DIR and TEE_LIBRARY must point to the OP-TEE export-ca devkit")
    endif()

    add_executable(token-signer-daemon
        apps/token-signer-daemon/token_signer_daemon_main.c
        apps/token-signer-daemon/token_signer_daemon.c
        apps/token-signer-daemon/token_signer_tee.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/t_cose/src/t_cose_sign1_sign.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/t_cose/src/t_cose_util.c
    )
    target_include_directories(token-signer-daemon PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/token-signer-daemon
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/package
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
        ${CMAKE_CURRENT_SOURCE_DIR}/src/security
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/t_cose/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/t_cose/src
        ${CMAKE_CURRENT_SOURCE_DIR}/optee/ecdsa-p256-sign/ta/include
        ${TEE_INCLUDE_DIR}
    )
    target_compile_definitions(token-signer-daemon PRIVATE
        _GNU_SOURCE
        T_COSE_USE_OPENSSL_CRYPTO
        T_COSE_DISABLE_ES384
        T_COSE_DISABLE_ES512
        T_COSE_DISABLE_EDDSA
        T_COSE_DISABLE_PS256
        T_COSE_DISABLE_PS384
        T_COSE_DISABLE_PS512
        T_COSE_DISABLE_SHORT_CIRCUIT_SIGN
    )
    target_link_libraries(token-signer-daemon PRIVATE
        gateway-qcbor
        OpenSSL::Crypto
        ${TEE_LIBRARY}
    )
endif()

add_executable(uds_multiframe_smoke
    tools/smoke/uds_multiframe_smoke.c
)
target_compile_definitions(uds_multiframe_smoke PRIVATE _GNU_SOURCE)
target_link_libraries(uds_multiframe_smoke PRIVATE gateway-core)

if(BUILD_TESTING)
add_test(
    NAME verify-raw-can-script-contract
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_verify_raw_can_script.sh
)

add_test(
    NAME cose-cwt-token-contract
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cose_cwt_token_contract.sh
)

endif()
