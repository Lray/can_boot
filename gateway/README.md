# Single-MCU Secure Updater

面向 T527 Linux + 单个 STM32U5 MCU 的高可靠安全升级组件。生产链路使用
hawkBit/SWUpdate 接收和验证发布包，经固定 SocketCAN/ISO-TP/UDS 通道更新唯一 MCU，
并由 MCUboot 完成镜像签名、安全计数器、A/B 启动和回滚裁决。

本产品明确不支持多 MCU 清单、动态 CAN ID、节点发现或并行调度。固定
`0x7E0/0x7E8` 是单 MCU 产品接口的一部分，而不是待扩展的临时实现。产品范围和
非目标见 `docs/single-mcu-product-scope.md`。

工作目录：`E:\T527\can_boot\gateway`

## 当前状态

| 范围 | 状态 | 说明 |
|---|---|---|
| RAW CAN | PASS | 真实 `0x700` heartbeat 与 `0x7E0/0x7E8` 双向通信已验证 |
| ISO-TP | PASS | 单帧、多帧、BS/STmin 与 timeout 路径已验证 |
| Minimal UDS | PASS | `0x10`、`0x3E`、`0x22` 基础诊断已验证 |
| Download/OTA validation | PASS | 下载、写入边界、update-state、verify、pending、reset 与故障场景已验证 |
| OTA 功能链 | PASS（受控验证环境） | OTA、Transfer 与 SecurityAccess 已完成实机验证 |
| MCU ULog 诊断工具 | PASS | 独立 raw-CAN 接收器可按需手工运行，不参与 OTA |
| Production trust chain | OPEN | 当前参考包/签发流程仍属于测试信任域，不能标记为生产信任链 |
| 企业级生产发布 | NOT READY | 尚缺 CI、版本标签、SBOM、签名 Release 与密钥生命周期治理 |

## 架构边界

- `src/transport/`：RAW CAN、ISO-TP 与传输回调边界。
- `src/uds/`：UDS client 与 NRC/timeout 处理。
- `src/security/`：Gateway 侧 SecurityAccess 编排与 Token 交互。
- `src/package/`：升级包结构、CRC/SHA、MCUboot 格式边界与策略校验；不作最终签名/启动裁决。
- `src/ota/`：单 MCU 更新入口、`0x34/0x36/0x37` 下载、断点续传、reset 与确认。
- `src/log/`：MCU ULog raw-CAN 重组；仅供独立诊断工具使用，独立于 OTA 会话。
- `tools/smoke/`：板端 smoke runner；不得在这里复制协议实现。

完整边界定义见 `docs/module-boundaries.md`。

## 仓库布局

```text
gateway/
├── CMakeLists.txt
├── src/                         生产库源码（按职责分层）
│   ├── transport/               CAN/ISO-TP 传输
│   ├── uds/                     UDS client
│   ├── security/                SecurityAccess 编排
│   ├── package/                 升级包解析与密码杂项
│   ├── ota/                     单 MCU 更新流程
│   ├── log/                     MCU ULog raw-CAN 接收
├── apps/                        Linux 诊断入口、ULog receiver、TEE signer
├── adapters/                   SWUpdate/ZeroMQ、token signer 等外部适配
├── tools/                       smoke 与离线 scenario CLI 源码
├── scripts/                     构建与诊断脚本
├── tests/                       当前保留的回归测试
├── docs/                        设计与验收说明
└── t527_deploy                  历史 AArch64 验收 runner
```

历史阶段目录及对应 prepare 脚本已清理，不再是支持的交付入口。

## 构建

### Host 回归

```bash
cmake -S . -B build-host -DBUILD_TESTING=ON -DENABLE_SWUPDATE_BRIDGE=OFF
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
```

默认生产配置关闭 Gateway host tests；Host 环境不具备 ZeroMQ 开发包时关闭
SWUpdate adapter，仍会构建直连诊断 updater、package-probe、ULog receiver 和
板端 smoke targets。

该命令执行 CMake build 和当前保留的 CTest。host PASS 不能替代正式板端验收。

### T527 AArch64 target

在注册的 `Ubuntu-24.04` WSL 中执行：

```bash
SDK_BUILDROOT=/home/lirui/work/t527/MYD-LT527/out/t527/myd_lt527_emmc/buildroot/buildroot
LINUX_UAPI=/home/lirui/work/t527/MYD-LT527/out/t527/kernel/build/usr/include
TEE_DEVKIT=/home/lirui/work/t527/MYD-LT527/platform/allwinner/security/optee/plat/arm-plat-sun55iw3p1/export-ca
cmake -S . -B build-target \
  -DCMAKE_C_COMPILER="${SDK_BUILDROOT}/host/bin/aarch64-none-linux-gnu-gcc" \
  -DCMAKE_SYSROOT="${SDK_BUILDROOT}/staging" \
  -DLINUX_UAPI_INCLUDE_DIR="${LINUX_UAPI}" \
  -DENABLE_SWUPDATE_BRIDGE=ON \
  -DENABLE_TOKEN_SIGNER_DAEMON=ON \
  -DZMQ_INCLUDE_DIR="${SDK_BUILDROOT}/staging/usr/include" \
  -DZMQ_LIBRARY="${SDK_BUILDROOT}/staging/usr/lib/libzmq.so" \
  -DTEE_INCLUDE_DIR="${TEE_DEVKIT}/include" \
  -DTEE_LIBRARY="${TEE_DEVKIT}/exportlib/libteec.so.1"
cmake --build build-target -j --target \
  mcu-updater mcu-updater-direct token-signer-daemon
```

生产构建必须使用 AArch64 compiler、目标 sysroot 和目标 ZeroMQ，并生成：

```text
build-target/mcu-updater
build-target/mcu-updater-direct
build-target/token-signer-daemon
```

## 部署到 T527

生产系统只部署 `mcu-updater` 和 `token-signer-daemon`，由 systemd 分别管理
网络、SWUpdate、更新服务和 signer：

```powershell
.\scripts\deploy_mcu_update_systemd_adb.ps1 <release arguments>
```

直连诊断工具不属于生产 runtime，由独立脚本部署：

```powershell
.\scripts\deploy_mcu_update_direct_adb.ps1 <diagnostic arguments>
.\scripts\run_mcu_update_direct_adb.ps1 -ImagePath <image.bin>
```

## Runner 命令

```text
mcu-updater <device-local Remote Handler options>
mcu-updater-direct <options>
mcu-package-probe <image.bin>
gateway-log-receiver <ifname>
```

只读诊断示例：

```bash
cd /opt/can-ota-gateway
./gateway-log-receiver awlink0
```

真实 OTA 的 COSE/CWT token 只能由独立的 OP-TEE signer 根据当前 MCU seed
即时生成。禁止把 token、seed、private key 或原始 `27xx/67xx` 帧写入命令历史、
日志、证据包或仓库。

在生产信任链关闭前，上述命令仅用于明确授权的受控验收场景。

## 诊断与板端验收约束

- MCU ULog 接收器只监听 raw-CAN `0x6D0`；它不发送 UDS 请求、不参与 OTA 状态机，
  也不由 MCU updater 启动或管理。
- SecurityAccess 请求/响应不得写入命令历史、仓库或诊断输出。
- heartbeat/link/CAN 异常、意外 NRC、timeout、DID/CRC/sequence 不一致时立即停止，
  不自动重试或跳过失败步骤。

## 固定协议参数

| 参数 | 值 |
|---|---|
| Classic CAN bitrate | 500000 |
| Gateway request ID | `0x7E0` |
| MCU response ID | `0x7E8` |
| MCU heartbeat ID | `0x700`，payload 以 `A5` 开头 |
| ISO-TP | `BS=8`，`STmin=2 ms` |
| UDS timing | 初始默认 `P2=50 ms`、`P2*=5000 ms`；每次 `0x10` 后以 MCU 公布的参数为准（P2* wire unit=10 ms），TesterPresent=1000 ms |
| TransferData payload | 256 bytes |
| maxNumberOfBlockLength | 258 |
| RequestDownload extension | `payload_id`, target slot, resume offset |
| Update checkpoint | 8192 bytes |

共享常量以 `src/profile.h` 为准；README 与代码冲突时必须先修正文档和
契约，再修改生产实现。

## 已知生产化缺口

- 生成信任链仍使用测试 trust profile；尚未接入离线根/HSM、签发审批、
  key rotation、revocation 与审计。
- 当前没有 CI/CD、版本标签、CODEOWNERS、项目级 LICENSE、SBOM、覆盖率和静态
  分析门禁。
- `t527_deploy` 与参考升级包直接保存在 Git 中，尚未迁移到签名制品库。
- 尚未实现 cloud download、delta、compression、multi-MCU orchestration 或
  production package scheduler。
- README 中的状态是当前工程事实，不等同于功能安全、信息安全或量产认证。

在上述缺口关闭并形成可重复、可审计、带标签的干净 Release 前，本仓库应标记为
`pre-production`，不能标记为企业级量产就绪。

## 参考文档

- `docs/module-boundaries.md`
- `docs/bringup-checklist.md`
- `docs/ota-executor.md`
