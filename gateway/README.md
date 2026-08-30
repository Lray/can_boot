# CAN Gateway

面向 T527 Linux/SocketCAN 的 CAN OTA 网关。项目使用 C99 实现 RAW CAN、
ISO-TP、UDS、固件传输、断点续传、包结构预检、ECU reset，并与 STM32U5 ECU 的
MCUboot A/B Slot 流程配合。

工作目录：`E:\T527\can_boot\gateway`

## 当前状态

| 范围 | 状态 | 说明 |
|---|---|---|
| RAW CAN | PASS | 真实 `0x700` heartbeat 与 `0x7E0/0x7E8` 双向通信已验证 |
| ISO-TP | PASS | 单帧、多帧、BS/STmin 与 timeout 路径已验证 |
| Minimal UDS | PASS | `0x10`、`0x3E`、`0x22` 基础诊断已验证 |
| Download/OTA validation | PASS | 下载、写入边界、update-state、verify、pending、reset 与故障场景已验证 |
| OTA 功能链 | PASS（受控验证环境） | OTA、Transfer 与 SecurityAccess 已完成实机验证 |
| ECU ULog 诊断工具 | PASS | 独立 raw-CAN 接收器可按需手工运行，不参与 OTA |
| Production trust chain | OPEN | 当前参考包/签发流程仍属于测试信任域，不能标记为生产信任链 |
| 企业级生产发布 | NOT READY | 尚缺 CI、版本标签、SBOM、签名 Release 与密钥生命周期治理 |

## 架构边界

- `src/transport/`：RAW CAN、ISO-TP 与传输回调边界。
- `src/uds/`：UDS client 与 NRC/timeout 处理。
- `src/security/`：Gateway 侧 SecurityAccess 编排与 Token 交互。
- `src/package/`：升级包结构、CRC/SHA、MCUboot 格式边界与策略校验；不作最终签名/启动裁决。
- `src/ota/`：`0x34/0x36/0x37` 下载、断点续传、reset 与 OTA 编排。
- `src/log/`：ECU ULog raw-CAN 重组；仅供独立诊断工具使用，独立于 OTA 会话。
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
│   ├── ota/                    OTA 流程
│   ├── log/                     ECU ULog raw-CAN 接收
│   └── ota/                     OTA 编排
├── apps/                       Linux 进程入口（OTA worker、ULog receiver）
├── adapters/                   SWUpdate/ZeroMQ、token signer 等外部适配
├── tools/                       smoke 与离线 scenario CLI 源码
├── scripts/                     构建与诊断脚本
├── tests/                       当前保留的回归测试
├── docs/                        设计与验收说明
├── t527_deploy                  已验证 AArch64 runner
└── upgrade_package_v1.2.46/     一份参考升级包
```

历史阶段目录及对应 prepare 脚本已清理，不再是支持的交付入口。

## 构建

### Host 回归

```bash
bash scripts/build_host_wsl.sh
```

该脚本显式设置 `BUILD_TESTING=ON`；默认生产配置关闭 Gateway host tests，
只生成 worker、package-probe、ULog receiver 和板端 smoke targets。

该命令执行 CMake build 和当前保留的 CTest。host PASS 不能替代正式板端验收。

### T527 AArch64 target

在注册的 `Ubuntu-24.04` WSL 中执行：

```bash
cd /mnt/e/T527/can_boot/gateway
bash scripts/build_target_wsl.sh build-target
```

脚本只接受 AArch64 compiler，并生成：

```text
build-target/gateway-ota-worker-v1
build-target/gateway-ota-package-probe
build-target/gateway-log-receiver
build-target/raw_can_smoke
build-target/uds_smoke
```

仓库中的 `t527_deploy` 是最终板端 runner 的保留名称，不是构建目录。

## 当前保留交付物

### T527 runner

```text
file:   t527_deploy
size:   134936 bytes
sha256: C0E273302C3579E7CF172E867E3539C40FACB44E7038164D64BC9B94E31A27EE
```

### 参考升级包

```text
directory: upgrade_package_v1.2.46
target:    Slot1
version:   1.2.46+0
SEC_CNT:   11
image:     131072 bytes
image sha: 2307EDE89CBBA17729244E0C67B75E4AC2A164B3C0620AD76CC2A8079246F109
```

目录内保留 `image.bin`、`payload-secondary.bin` 及 provenance 描述。该包来自已完成
SOAK 的测试信任域，只能作为参考/受控验收输入，不能作为生产签名包发布。

## 部署到 T527

目标设备通过 ADB 可见后：

```bash
adb -s <serial> shell "mkdir -p /opt/can-ota-gateway"
adb -s <serial> push t527_deploy /opt/can-ota-gateway/t527_deploy
adb -s <serial> push upgrade_package_v1.2.46 /opt/can-ota-gateway/
adb -s <serial> shell "chmod 0755 /opt/can-ota-gateway/t527_deploy"
```

部署后必须在板端重新核对 runner 与 image 的 SHA。若目标路径已有不同
SHA 的 runner 或包，应停止，不覆盖旧对象后继续测试。

## Runner 命令

```text
gateway-ota-worker-v1 <options>
gateway-ota-package-probe <image.bin>
gateway-log-receiver <ifname>
```

`t527_deploy` 是遗留预编译验收工具，其 OTA 子命令仍按 v2 包参数（manifest.bin）
编写，未随 v3 包格式更新，使用前必须基于新代码重新构建。

只读诊断示例：

```bash
cd /opt/can-ota-gateway
./gateway-log-receiver awlink0
```

真实 OTA 需要一次性、与当前 ECU seed 匹配的 COSE/CWT token。只允许通过临时
token 文件提供，禁止把 token、seed、private key 或原始 `27xx/67xx` 帧写入
命令历史、日志、证据包或仓库：

```bash
OTA_TOKEN_FILE=/run/secrets/ota-token.cbor \
./gateway-ota-worker-v1 --job-dir /run/ecu-ota/var/jobs/<job-id> ...
```

在生产信任链关闭前，上述命令仅用于明确授权的受控验收场景。

## 诊断与板端验收约束

- ECU ULog 接收器只监听 raw-CAN `0x6D0`；它不发送 UDS 请求、不参与 OTA 状态机，
  也不由 OTA worker 启动或管理。
- SecurityAccess 请求/响应不得写入命令历史、仓库或诊断输出。
- heartbeat/link/CAN 异常、意外 NRC、timeout、DID/CRC/sequence 不一致时立即停止，
  不自动重试或跳过失败步骤。

## 固定协议参数

| 参数 | 值 |
|---|---|
| Classic CAN bitrate | 500000 |
| Gateway request ID | `0x7E0` |
| ECU response ID | `0x7E8` |
| ECU heartbeat ID | `0x700`，payload 以 `A5` 开头 |
| ISO-TP | `BS=8`，`STmin=2 ms` |
| UDS timing | 初始默认 `P2=50 ms`、`P2*=5000 ms`；每次 `0x10` 后以 ECU 公布的参数为准（P2* wire unit=10 ms），TesterPresent=1000 ms |
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
- 尚未实现 cloud download、delta、compression、multi-ECU orchestration 或
  production package scheduler。
- README 中的状态是当前工程事实，不等同于功能安全、信息安全或量产认证。

在上述缺口关闭并形成可重复、可审计、带标签的干净 Release 前，本仓库应标记为
`pre-production`，不能标记为企业级量产就绪。

## 参考文档

- `docs/module-boundaries.md`
- `docs/bringup-checklist.md`
- `docs/ota-executor.md`
