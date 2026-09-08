#!/bin/bash
# T527 交叉编译脚本 (Debug版本)

set -e

# 颜色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}=== Cross Compiling Gateway OTA Worker (Debug) ===${NC}"

# T527 SDK路径
SDK_ROOT=~/work/t527/MYD-LT527
TOOLCHAIN_ROOT=$SDK_ROOT/out/t527/myd_lt527_emmc/buildroot/buildroot/host
SYSROOT=$TOOLCHAIN_ROOT/aarch64-buildroot-linux-gnu/sysroot

# 交叉编译工具链（使用完整路径）
export CC=$TOOLCHAIN_ROOT/bin/aarch64-none-linux-gnu-gcc
export CXX=$TOOLCHAIN_ROOT/bin/aarch64-none-linux-gnu-g++
export AR=$TOOLCHAIN_ROOT/bin/aarch64-none-linux-gnu-ar
export AS=$TOOLCHAIN_ROOT/bin/aarch64-none-linux-gnu-as
export LD=$TOOLCHAIN_ROOT/bin/aarch64-none-linux-gnu-ld
export STRIP=$TOOLCHAIN_ROOT/bin/aarch64-none-linux-gnu-strip

# 检查编译器是否存在
if [ ! -f "$CC" ]; then
    echo -e "${YELLOW}Error: Compiler not found at $CC${NC}"
    echo -e "Please check SDK path: $SDK_ROOT"
    exit 1
fi
echo -e "${GREEN}Using compiler: $CC${NC}"

# 清理旧的build目录
BUILD_DIR="build-cross-debug"
if [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Cleaning old build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

# 创建build目录
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Linux UAPI头文件路径（包含isotp.h）
LINUX_UAPI=$SDK_ROOT/out/t527/myd_lt527_emmc/buildroot/buildroot/build/can-utils-2021.08.0/include

# 配置CMake（交叉编译 + Debug模式）
echo -e "${GREEN}Configuring CMake (Cross-compile Debug)...${NC}"
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=$CC \
    -DCMAKE_CXX_COMPILER=$CXX \
    -DCMAKE_FIND_ROOT_PATH=$SYSROOT \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_SYSROOT=$SYSROOT \
    -DENABLE_SWUPDATE_BRIDGE=ON \
    -DLINUX_UAPI_INCLUDE_DIR=$LINUX_UAPI \
    -DZMQ_INCLUDE_DIR=$SYSROOT/usr/include \
    -DZMQ_LIBRARY=$SYSROOT/usr/lib/libzmq.so

# 编译
echo -e "${GREEN}Building...${NC}"
make -j$(nproc)

# 显示生成的可执行文件
echo -e "${GREEN}=== Build Complete ===${NC}"
echo -e "Debug binaries:"
ls -lh mcu-updater 2>/dev/null && file mcu-updater
ls -lh mcu-updater-direct 2>/dev/null && file mcu-updater-direct

# 检查是否是ARM二进制
echo -e "\n${GREEN}Verifying ARM binary:${NC}"
file mcu-updater | grep -i aarch64 && echo -e "${GREEN}✓ Correct architecture${NC}" || echo -e "${YELLOW}⚠ Wrong architecture${NC}"

# 检查调试符号
echo -e "\n${GREEN}Checking debug symbols:${NC}"
if file mcu-updater | grep -q "not stripped"; then
    echo -e "${GREEN}✓ Debug symbols present${NC}"
else
    echo -e "${YELLOW}⚠ No debug symbols (stripped)${NC}"
fi

echo -e "\n${GREEN}To deploy to device:${NC}"
echo -e "  adb push mcu-updater /tmp/mcu-updater-debug"
echo -e "\n${GREEN}To debug remotely:${NC}"
echo -e "  On device: gdbserver :1234 /tmp/mcu-updater-debug"
echo -e "  On PC: gdb-multiarch mcu-updater"
echo -e "         (gdb) target remote 10.112.4.229:1234"
