# Gateway WSL 构建配置

## 问题说明
VSCode 无法跳转到 `tee_client_api.h` 和其他 SDK 头文件，因为：
1. 缺少 compile_commands.json（IDE 智能感知的索引数据库）
2. TEE SDK 路径未配置到构建系统中

## 解决步骤

### 1. 在 WSL 中安装依赖

```bash
# 安装基础构建工具
sudo apt update
sudo apt install build-essential cmake

# 安装 OP-TEE 客户端库（如果使用 token-signer-daemon）
# 具体安装方式取决于你的 TEE SDK 位置
```

### 2. 配置并构建项目

在 WSL 终端中，切换到 gateway 目录：

```bash
cd /mnt/e/T527/can_boot/gateway

# 创建构建目录
mkdir -p build
cd build

# 配置 CMake（基础版本，不包含 TEE）
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DBUILD_TESTING=ON \
      ..

# 如果需要构建 token-signer-daemon（需要 TEE SDK）
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DBUILD_TESTING=ON \
      -DENABLE_TOKEN_SIGNER_DAEMON=ON \
      -DTEE_INCLUDE_DIR=/path/to/optee/export-ca/include \
      -DTEE_LIBRARY=/path/to/optee/export-ca/lib/libteec.so \
      ..

# 构建
make -j$(nproc)
```

**关键参数说明：**
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON` - 生成 compile_commands.json
- `TEE_INCLUDE_DIR` - OP-TEE 客户端头文件路径（包含 tee_client_api.h）
- `TEE_LIBRARY` - libteec 库路径

### 3. 验证 compile_commands.json

```bash
# 检查文件是否生成
ls -lh build/compile_commands.json

# 检查是否包含 TEE 头文件路径
grep -i "tee_client_api" build/compile_commands.json
grep -i "TEE_INCLUDE_DIR" build/compile_commands.json
```

### 4. 重新加载 VSCode

在 VSCode 中：
1. 按 `Ctrl+Shift+P`
2. 运行 `C/C++: Reset IntelliSense Database`
3. 运行 `Developer: Reload Window`

### 5. 查找 TEE SDK 路径

如果不确定 TEE SDK 安装位置：

```bash
# 查找 tee_client_api.h
find /usr -name "tee_client_api.h" 2>/dev/null
find /opt -name "tee_client_api.h" 2>/dev/null
find ~ -name "tee_client_api.h" 2>/dev/null

# 查找 libteec
find /usr -name "libteec.so*" 2>/dev/null
find /opt -name "libteec.so*" 2>/dev/null

# 或者使用 pkg-config（如果 TEE 提供了）
pkg-config --cflags libteec
pkg-config --libs libteec
```

## 常见问题

### Q: 还是无法跳转到 SDK 头文件
**A:** 确保：
1. compile_commands.json 确实包含了 SDK 的 include 路径
2. VSCode 使用的是 WSL 扩展，而不是 Windows 本地打开
3. 检查 `.vscode/c_cpp_properties.json` 中的路径是否正确
4. 尝试切换 IntelliSense 引擎：C/C++ 扩展 vs clangd 扩展

### Q: 使用 clangd 还是 C/C++ IntelliSense？
**A:** 两种方案：
- **C/C++ 扩展**：使用 `.vscode/c_cpp_properties.json`（已配置）
- **clangd 扩展**：使用 `.clangd` 配置（can 目录已有）

可以在 VSCode 设置中禁用其中一个以避免冲突。

### Q: 如何为 gateway 添加 .clangd 配置？
**A:** 创建 `gateway/.clangd`：

```yaml
CompileFlags:
  CompilationDatabase: build
```
