# Host-based unit tests for ECU modules.
# Configure independently with: cmake -S Tests -B build-host.
# Only pure codecs and host-executable protocol state machines belong here.
# Flash, boot, persistence, security state, and HAL behavior are board/HIL
# concerns.

cmake_minimum_required(VERSION 3.22)

# Permit this directory to configure independently with a host C compiler.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CanHostTests LANGUAGES C)
    enable_testing()
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(TRANSPORT_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/../transport)
set(UDS_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/../uds)
set(CORE_INC_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/../Core/Inc)
set(REPO_INC_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/../..)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/isotp.cmake)
can_add_isotp(${CMAKE_CURRENT_BINARY_DIR}/isotp-c)

set(TEST_MODULE_INC_DIRS
    ${REPO_INC_DIR}
    ${TRANSPORT_DIR}
    ${UDS_DIR}
    ${CORE_INC_DIR}
)

function(configure_host_test target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Werror
            -pedantic
            -Wno-unused-parameter
        )
    endif()
    if(CMAKE_CROSSCOMPILING)
        target_link_options(${target} PRIVATE --specs=nosys.specs)
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()

# ---- uds_msg host test ----
add_executable(uds_msg_host_test
    uds_msg_host_test.c
    ${UDS_DIR}/uds_msg.c
)
target_include_directories(uds_msg_host_test PRIVATE ${TEST_MODULE_INC_DIRS})
configure_host_test(uds_msg_host_test)

# ---- raw ULog CAN wire host test ----
add_executable(ulog_can_wire_host_test
    ulog_can_wire_host_test.c
    ${TRANSPORT_DIR}/ulog_can_wire.c
)
target_include_directories(ulog_can_wire_host_test PRIVATE ${TEST_MODULE_INC_DIRS})
configure_host_test(ulog_can_wire_host_test)

# ---- ISO-TP host test ----
add_executable(isotp_host_test
    isotp_host_test.c
)
target_include_directories(isotp_host_test
    PRIVATE ${TEST_MODULE_INC_DIRS}
)
target_link_libraries(isotp_host_test PRIVATE simon_cahill::isotp_c)
configure_host_test(isotp_host_test)

# ---- STM32 ISO-TP port test with fake CMSIS registers ----
add_executable(isotp_stm32_host_test
    isotp_stm32_host_test.c
    ${TRANSPORT_DIR}/can_transport.c
    ${TRANSPORT_DIR}/isotp_stm32.c
)
target_include_directories(isotp_stm32_host_test BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes
    ${TEST_MODULE_INC_DIRS}
)
target_link_libraries(isotp_stm32_host_test PRIVATE simon_cahill::isotp_c)
configure_host_test(isotp_stm32_host_test)

# ---- download and checkpoint/identity transaction host test ----
add_executable(download_resume_host_test
    download_resume_host_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../download/download.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../download/erase_job.c
    ${REPO_INC_DIR}/shared/crc32.c
)
target_include_directories(download_resume_host_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../boot
    ${CMAKE_CURRENT_SOURCE_DIR}/../download
    ${REPO_INC_DIR}
)
configure_host_test(download_resume_host_test)
