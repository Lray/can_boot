cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# The generated Cube/HAL sources use C99 constructs but do not require C11.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Application image placement. Slot presets override these values for MCUboot
# payloads, while the ordinary Debug and Release presets retain full flash.
set(CAN_FLASH_ORIGIN "0x08000000" CACHE STRING "Flash origin for the application image")
set(CAN_FLASH_LENGTH "4096K" CACHE STRING "Flash length available to the application image")

# Set the project name
set(CMAKE_PROJECT_NAME Can)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME} LANGUAGES C ASM)
message("Build type: " ${CMAKE_BUILD_TYPE})

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Configure a per-build linker script so boot-slot presets cannot modify the
# canonical linker-script template used by normal firmware builds.
set(CAN_FLASH_LINKER_SCRIPT "${CMAKE_BINARY_DIR}/STM32U5a9xx_FLASH.ld")
configure_file(
    "${CMAKE_SOURCE_DIR}/STM32U5a9xx_FLASH.ld"
    "${CAN_FLASH_LINKER_SCRIPT}"
    @ONLY
)
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "-T${CAN_FLASH_LINKER_SCRIPT}")
set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CAN_FLASH_LINKER_SCRIPT}")

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Third-party libraries
include(cmake/thirdparty.cmake)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# The STM32CubeIDE/CMake toolchain normally provides CMAKE_OBJCOPY.  Resolve a
# PATH-only installation as a fallback, but never silently use the host
# objcopy for an ARM firmware image.
get_property(CAN_ARM_OBJCOPY_CACHE CACHE CAN_ARM_OBJCOPY PROPERTY VALUE)
if(CAN_ARM_OBJCOPY_CACHE AND IS_ABSOLUTE "${CAN_ARM_OBJCOPY_CACHE}")
    set(CMAKE_OBJCOPY "${CAN_ARM_OBJCOPY_CACHE}")
endif()
if(NOT CMAKE_OBJCOPY OR CMAKE_OBJCOPY MATCHES "-NOTFOUND$" OR
   NOT IS_ABSOLUTE "${CMAKE_OBJCOPY}")
    find_program(CAN_OBJCOPY_EXECUTABLE
        NAMES arm-none-eabi-objcopy st-arm-none-eabi-objcopy
    )
    if(CAN_OBJCOPY_EXECUTABLE)
        set(CMAKE_OBJCOPY "${CAN_OBJCOPY_EXECUTABLE}")
    endif()
endif()

if(NOT CMAKE_OBJCOPY OR CMAKE_OBJCOPY MATCHES "-NOTFOUND$")
    message(FATAL_ERROR
        "No ARM objcopy found. Configure STM32CubeIDE's GNU Arm toolchain "
        "or add arm-none-eabi-objcopy to PATH."
    )
endif()

# Add application source files (responsibility-based module directories)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    boot/boot_observation.c
    boot/image_confirm.c
    boot/flash_map.c
    download/download.c
    download/erase_job.c
    ../shared/crc32.c
    security/security_access.c
    security/security_access_entropy_stm32.c
    security/security_token.c
    security/security_token_cose.c
    security/security_crypto_port.c
    transport/can_driver_stm32.c
    transport/can_transport.c
    transport/isotp_stm32.c
    transport/ulog_can.c
    transport/ulog_can_wire.c
    uds/uds_msg.c
    uds/uds_read_did.c
    uds/uds_server.c
    watchdog/watchdog.c
)

# Add include paths (module directories + CubeMX Core/Inc)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/boot
    ${CMAKE_CURRENT_SOURCE_DIR}/download
    ${CMAKE_CURRENT_SOURCE_DIR}/security
    ${CMAKE_CURRENT_SOURCE_DIR}/transport
    ${CMAKE_CURRENT_SOURCE_DIR}/uds
    ${CMAKE_CURRENT_SOURCE_DIR}/watchdog
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    qbor
    tcose
    tinycrypt
    simon_cahill::isotp_c
)

# Post-build: generate .bin and .hex for STM32CubeIDE/CubeProgrammer and
# manual bootloader provisioning.
set(CAN_BIN_OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin"
)
set(CAN_HEX_OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex"
)
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary
            "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
            "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/$<TARGET_FILE_BASE_NAME:${CMAKE_PROJECT_NAME}>.bin"
    COMMAND "${CMAKE_OBJCOPY}" -O ihex
            "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
            "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/$<TARGET_FILE_BASE_NAME:${CMAKE_PROJECT_NAME}>.hex"
    BYPRODUCTS
            "${CAN_BIN_OUTPUT}"
            "${CAN_HEX_OUTPUT}"
    VERBATIM
    COMMENT "Generating .bin and .hex"
)
