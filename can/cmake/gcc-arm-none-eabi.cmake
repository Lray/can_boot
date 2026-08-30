set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_ID GNU)

# Some default GCC settings
set(TOOLCHAIN_PREFIX                arm-none-eabi-)

# Prefer the GNU Arm bundle installed by STM32CubeIDE/Cube for VSCode.  The
# fallback keeps this toolchain usable with a standalone GNU Arm installation
# that is already available through PATH (including CI and Linux builds).
set(CAN_GNU_ARM_BIN_DIR "")
if(DEFINED ENV{CUBE_BUNDLE_PATH} AND EXISTS "$ENV{CUBE_BUNDLE_PATH}")
    set(CAN_CUBE_BUNDLE_ROOT "$ENV{CUBE_BUNDLE_PATH}")
elseif(CMAKE_HOST_WIN32 AND DEFINED ENV{LOCALAPPDATA})
    set(CAN_CUBE_BUNDLE_ROOT "$ENV{LOCALAPPDATA}/stm32cube/bundles")
endif()

if(CAN_CUBE_BUNDLE_ROOT AND EXISTS "${CAN_CUBE_BUNDLE_ROOT}/gnu-tools-for-stm32")
    file(GLOB CAN_GNU_ARM_BIN_CANDIDATES
        LIST_DIRECTORIES true
        "${CAN_CUBE_BUNDLE_ROOT}/gnu-tools-for-stm32/*/bin"
    )
    list(SORT CAN_GNU_ARM_BIN_CANDIDATES ORDER DESCENDING)
    foreach(CAN_GNU_ARM_BIN_CANDIDATE IN LISTS CAN_GNU_ARM_BIN_CANDIDATES)
        if(EXISTS "${CAN_GNU_ARM_BIN_CANDIDATE}/${TOOLCHAIN_PREFIX}gcc.exe" OR
           EXISTS "${CAN_GNU_ARM_BIN_CANDIDATE}/${TOOLCHAIN_PREFIX}gcc")
            set(CAN_GNU_ARM_BIN_DIR "${CAN_GNU_ARM_BIN_CANDIDATE}")
            break()
        endif()
    endforeach()
endif()

if(CAN_GNU_ARM_BIN_DIR)
    find_program(CAN_ARM_GCC
        NAMES ${TOOLCHAIN_PREFIX}gcc ${TOOLCHAIN_PREFIX}gcc.exe
        HINTS "${CAN_GNU_ARM_BIN_DIR}"
        NO_DEFAULT_PATH
    )
    find_program(CAN_ARM_GXX
        NAMES ${TOOLCHAIN_PREFIX}g++ ${TOOLCHAIN_PREFIX}g++.exe
        HINTS "${CAN_GNU_ARM_BIN_DIR}"
        NO_DEFAULT_PATH
    )
    find_program(CAN_ARM_OBJCOPY
        NAMES ${TOOLCHAIN_PREFIX}objcopy ${TOOLCHAIN_PREFIX}objcopy.exe
        HINTS "${CAN_GNU_ARM_BIN_DIR}"
        NO_DEFAULT_PATH
    )
    find_program(CAN_ARM_SIZE
        NAMES ${TOOLCHAIN_PREFIX}size ${TOOLCHAIN_PREFIX}size.exe
        HINTS "${CAN_GNU_ARM_BIN_DIR}"
        NO_DEFAULT_PATH
    )
else()
    set(CAN_ARM_GCC                ${TOOLCHAIN_PREFIX}gcc)
    set(CAN_ARM_GXX                ${TOOLCHAIN_PREFIX}g++)
    set(CAN_ARM_OBJCOPY            ${TOOLCHAIN_PREFIX}objcopy)
    set(CAN_ARM_SIZE               ${TOOLCHAIN_PREFIX}size)
endif()

set(CMAKE_C_COMPILER                ${CAN_ARM_GCC})
set(CMAKE_ASM_COMPILER              ${CAN_ARM_GCC})
set(CMAKE_CXX_COMPILER              ${CAN_ARM_GXX})
set(CMAKE_LINKER                    ${CAN_ARM_GXX})
set(CMAKE_OBJCOPY                   ${CAN_ARM_OBJCOPY})
set(CMAKE_SIZE                      ${CAN_ARM_SIZE})

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# MCU specific flags
set(TARGET_FLAGS "-mcpu=cortex-m33 -mfpu=fpv4-sp-d16 -mfloat-abi=hard ")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TARGET_FLAGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS} -x assembler-with-cpp -MMD -MP")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fdata-sections -ffunction-sections")

set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_C_FLAGS_RELEASE "-Os -g0")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -g0")

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-rtti -fno-exceptions -fno-threadsafe-statics")

set(CMAKE_EXE_LINKER_FLAGS "${TARGET_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --specs=nano.specs")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Map=${CMAKE_PROJECT_NAME}.map -Wl,--gc-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--print-memory-usage")
set(TOOLCHAIN_LINK_LIBRARIES "m")
