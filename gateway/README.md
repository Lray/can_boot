# MCU Secure Updater

面向 T527 Linux + STM32U5 MCU 节点的高可靠安全升级组件。生产链路使用
hawkBit/SWUpdate 接收和验证发布包，`mcu-updater` 接收镜像后将包内出厂身份
提交给常驻 `LSS_gateway`，先分配未配置节点，再查询目标当前 Node-ID，经 SocketCAN/ISO-TP/UDS
更新被选中的 MCU，并由 MCUboot 完成镜像签名、
安全计数器、A/B 启动和回滚裁决。

当前产品采用单 MCU，每次 OTA 前从 Node-ID `2` 开始配置未分配节点。
CAN ID 由目标的 Node-ID 动态派生，事务串行执行。多 MCU 产品化前必须补充
可恢复的 Node-ID 分配策略和多节点 HIL，见 `docs/multi-mcu-product-scope.md`。

`LSS_gateway` 固定使用 `awlink0`，持有唯一的 LSS Master 和官方 Gateway ASCII 实例，
按官方 `CO_main_basic.c` 和 `CO_epoll_interface.c` 的组织方式初始化模块、
处理 CAN 收发和协议计时，并被动接收命令。
`mcu-updater` 在一个独占 Unix socket 会话中请求 Fastscan/配置、选择、查询和取消选择，取得有效
Node-ID `1..127` 后调用原 OTA 函数。OTA 使用该 Node-ID 建立 ISO-TP，并通过
UDS `DID_FACTORY_IDENTITY` 校验升级前后的出厂身份。updater 不初始化 CANopen
或 LSS 实例。

工作目录：`E:\T527\can_boot\gateway`

## 当前状态

| 范围 | 状态 | 说明 |
|---|---|---|
| LSS 网关与客户端 | SDK 构建 / 客户端 QEMU PASS | 源码与链接边界、响应接收及四命令会话已验证；新链路实机待验 |
| RAW CAN | PASS（单 MCU HIL） | 单节点样机链路已验证；多节点隔离尚未完成 HIL |
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
- `src/package/`：读取升级包身份、实际长度和 MCUboot 声明版本；不作最终签名/启动裁决。
- `src/ota/`：单次目标 MCU 更新入口、`0x34/0x36/0x37` 下载、reset 与确认。
- `src/log/`：MCU ULog raw-CAN 重组；仅供独立诊断工具使用，独立于 OTA 会话。
- `apps/LSS_gateway/`：按官方 `CO_main_basic.c`、`CO_epoll_interface.c` 组织模块初始化和事件循环；ASCII 命令解析及 LSS 状态机由官方模块实现。
- `apps/mcu_updater/`：接收 SWUpdate 镜像，向常驻网关查询 Node-ID，执行 OTA。
- `third_party/CANopenLinux/`：固定版本原样 SocketCAN 驱动及必要头文件；项目配置位于 `src/CO_driver_conf.h`。
- `third_party/CANopenNode/`：固定版本原版 `CO_fifo`、`CO_LSSmaster` 和 `CO_gateway_ascii`。
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
│   ├── package/                 升级包身份、长度和版本读取
│   ├── ota/                     单次目标 MCU 更新流程
│   ├── log/                     MCU ULog raw-CAN 接收
├── apps/                        Linux 生产入口、诊断入口和 ULog receiver
├── adapters/                   SWUpdate/ZeroMQ 外部适配
├── tools/                       smoke 与离线 scenario CLI 源码
├── scripts/                     构建与诊断脚本
├── tests/                       当前保留的回归测试
├── docs/                        设计与验收说明
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
SWUpdate adapter，仍会构建 ULog receiver 和板端 smoke targets。

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
  -DZMQ_INCLUDE_DIR="${SDK_BUILDROOT}/staging/usr/include" \
  -DZMQ_LIBRARY="${SDK_BUILDROOT}/staging/usr/lib/libzmq.so" \
  -DTEE_INCLUDE_DIR="${TEE_DEVKIT}/include" \
  -DTEE_LIBRARY="${TEE_DEVKIT}/exportlib/libteec.so.1"
cmake --build build-target -j --target \
  LSS_gateway mcu-updater
```

生产构建必须使用 AArch64 compiler、目标 sysroot 和目标 ZeroMQ，并生成：

```text
build-target/LSS_gateway
build-target/mcu-updater
```

Remote endpoint 名称由每个 SWU 的 `images[].data` 指定，本项目使用 `image-install`；
它不是 SWUpdate 二进制的编译参数。详见 [运行说明](docs/systemd-hawkbit-runtime.md)。

## 部署到 T527

生产系统部署 `LSS_gateway.service` 和 `mcu-updater.service` 两个常驻服务。
updater 依赖常驻网关和
`tee-supplicant`，通过 `libteec` 直接请求专用 TA 签发 SecurityAccess token。
systemd 同时管理网络和 SWUpdate：

```powershell
.\scripts\deploy_mcu_update_systemd_adb.ps1 <release arguments>
```

## 进程与命令入口

```text
LSS_gateway
mcu-updater
gateway-log-receiver <ifname>
```

前两个进程由 systemd 启动。常驻网关通过
`/run/LSS_gateway/command.sock` 提供官方 LSS ASCII 命令。
每次 OTA 真正刷写前，updater 在同一个独占连接中逐条发送并等待响应：

```text
[1] lss_allnodes 100 2 1\r\n
[2] lss_switch_sel <vendorID> <productCode> <revisionNumber> <serialNumber>\r\n
[3] lss_get_node\r\n
[4] lss_switch_glob 0\r\n
```

`100` 是官方 Fastscan 单步等待时间，单位为毫秒；`2` 是起始 Node-ID，
`1` 要求 MCU 保存配置。官方 `lss_allnodes` 状态机重复扫描，直到没有未配置节点。
已配置节点不参与未配置节点 Fastscan，随后仍通过包内四字段 identity 精确选择和查询。
updater 取得 Node-ID `1..127` 并完成取消选择后，才建立 ISO-TP/UDS 通道。

网关只接受一个客户端连接。连接异常结束时调用 `CO_LSSmaster_swStateDeselect()`、
恢复 `CO_LSSmaster_DEFAULT_TIMEOUT` 并清除 Gateway 会话状态。
客户端接收以官方固定版本 `cocomm` 的阻塞读取实现为准；无独立寻址总超时。
`cocomm` 源码不保留在项目中，不编译、不部署、不调用。

网关只编译 `LSS_gateway.c`、`CO_driver.c`、`CO_fifo.c`、`CO_LSSmaster.c`、
`CO_gateway_ascii.c`；功能配置仅启用 Gateway ASCII LSS、LSS Master 和 ASCII FIFO。
固定版本 CANopenNode `9b8beed8367241e96ac03f916cd5a500bcb2cf23` 的官方源码保持原样。
命令解析和 Fastscan 均由官方模块完成，网关入口只连接 Socket、CAN 与官方 process 循环。

只读诊断示例：

```bash
cd /opt/can-ota-gateway
./gateway-log-receiver awlink0
```

真实 OTA 的 COSE/CWT token 只能由专用 OP-TEE TA 根据当前 MCU seed 即时生成。
禁止把 token、seed、private key 或原始 `27xx/67xx` 帧写入命令历史、
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
| Gateway request ID | `0x600 + Node-ID` |
| MCU response ID | `0x580 + Node-ID` |
| MCU heartbeat | `0x700 + Node-ID`，DLC=`1`，`DATA[0]=0x05`（Alive），周期 `1000 ms` |
| MCU offline 判定 | Gateway 接收端距最后一次有效 heartbeat 超过 `3000 ms`；不使用收发或错误计数 |
| ISO-TP | `BS=8`，`STmin=2 ms` |
| UDS timing | 初始默认 `P2=50 ms`、`P2*=5000 ms`；每次 `0x10` 后以 MCU 公布的参数为准（P2* wire unit=10 ms），诊断通信完成后重启 S3 |
| TransferData payload | 256 bytes |
| maxNumberOfBlockLength | 258 |
| RequestDownload | 标准 `34 00 44 <address[4]> <size[4]>` / `74 20 01 02`；MCU 自动选择 inactive slot |

共享常量以 `src/profile.h` 为准；README 与代码冲突时必须先修正文档和
契约，再修改生产实现。

## 已知生产化缺口

- 生成信任链仍使用测试 trust profile；尚未接入离线根/HSM、签发审批、
  key rotation、revocation 与审计。
- 当前没有 CI/CD、版本标签、CODEOWNERS、项目级 LICENSE、SBOM、覆盖率和静态
  分析门禁。
- 参考升级包尚未迁移到签名制品库。
- 尚未完成多 MCU HIL、并行调度、delta、compression 或 production package scheduler。
- README 中的状态是当前工程事实，不等同于功能安全、信息安全或量产认证。

在上述缺口关闭并形成可重复、可审计、带标签的干净 Release 前，本仓库应标记为
`pre-production`，不能标记为企业级量产就绪。

## 参考文档

- `docs/module-boundaries.md`
- `docs/bringup-checklist.md`
- `docs/ota-executor.md`
