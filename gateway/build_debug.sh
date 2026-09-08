#!/bin/bash
# Gateway OTA Worker 调试版本编译脚本

set -e  # 遇到错误立即退出

# 颜色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Building Gateway OTA Worker (Debug) ===${NC}"

# 清理旧的build目录（可选）
BUILD_DIR="build-debug"
if [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Cleaning old build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

# 创建build目录
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 配置CMake（Debug模式）
echo -e "${GREEN}Configuring CMake (Debug mode)...${NC}"
cmake .. \
    -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_SWUPDATE_BRIDGE=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# 编译
echo -e "${GREEN}Building...${NC}"
make -j$(nproc)

# 显示生成的可执行文件
echo -e "${GREEN}=== Build Complete ===${NC}"
echo -e "Debug binaries:"
ls -lh mcu-updater 2>/dev/null || echo "  mcu-updater: not built (need ENABLE_SWUPDATE_BRIDGE=ON)"
ls -lh mcu-updater-direct 2>/dev/null || true
ls -lh token-signer-daemon 2>/dev/null || true

# 检查调试符号
echo -e "\n${GREEN}Checking debug symbols:${NC}"
if [ -f "mcu-updater" ]; then
    file mcu-updater
    echo -e "${YELLOW}Note: Binary has debug symbols (not stripped)${NC}"
fi

echo -e "\n${GREEN}To deploy to device:${NC}"
echo -e "  adb push mcu-updater /tmp/mcu-updater-debug"
echo -e "\n${GREEN}To debug remotely:${NC}"
echo -e "  On device: gdbserver :1234 /tmp/mcu-updater-debug"
echo -e "  On PC: gdb-multiarch mcu-updater"
echo -e "         (gdb) target remote 10.112.4.229:1234"
