# MCU update 包格式

单次目标 MCU 更新的 package 只有一个成员：

| 成员 | 唯一职责 |
| --- | --- |
| `image.bin` | 16 字节目标身份包络，随后是 MCUboot 全槽签名镜像 |

`image.bin` 是 SWU 描述中的 remote artifact 名称。`package_input.h` 只定义
16 字节身份包络长度；本机不建立内部包目录。

## 传输、授权与事务缓冲

SWU 的唯一 remote artifact 是 `image.bin`（`type = "remote"`），经
Remote Handler ZMQ 通道到达 `mcu-updater`。生产集成要求 SWUpdate 使用现有的
“先验证再转发”行为：完整暂存 SWU artifact，完成 SWU 签名和描述中 artifact
hash 的校验后，才连接本机 Remote Handler 接收端并发送 `INIT` / `DATA`。
因此，只有已经通过 SWUpdate 发布授权校验的字节能够进入接收端。

`mcu-updater` 是与具体发布无关的常驻服务，不接受发布侧的 job ID、期望长度或
期望摘要。接收端从官方 `INIT:size` 获取本次声明长度，将 `DATA` 顺序写入只属于
当前事务的匿名 `memfd`，仅在实际字节数与声明长度完全一致后启动 MCU 更新。
尺寸越界、数据超量、提前结束或写入失败都不会启动 MCU 更新。

接收上限等于 `OTA_ENVELOPE_IDENTITY_SIZE + DEFAULT_SLOT_SIZE`（当前
16 + 131072 B）；这是接收资源上限，不再要求镜像等于整个 slot 大小。
updater 剥离身份包络后向 MCU 传输实际镜像长度。SWUpdate 负责验证 SWU 外层的签名与描述中的 artifact hash；本机
Remote Handler endpoint 仅允许受控的 SWUpdate 进程访问；MCU 的 MCUboot
验签、security counter、slot 选择与回滚仍是最终安全裁决。

本项目不启用 `hardware-compatibility`。updater 提取包络中的出厂身份，在一个
`/run/LSS_gateway/command.sock` 独占会话中依次请求 `lss_allnodes 100 2 1`、
按身份选择、查询 Node-ID 和取消选择。
常驻 `LSS_gateway` 将命令交给官方 Gateway ASCII/LSS Master 模块；updater 取得
当前有效 Node-ID `1..127` 后调用 OTA 函数。常驻 LSS 网关不接收镜像，
也不保存身份映射。OTA 通过
UDS `DID_FACTORY_IDENTITY` 校验升级前后的设备身份。镜像
适配约束由签名发布流程、MCUboot header 读取和 MCU 侧最终校验共同承担。

## 字段来源与推导规则

`image.bin` 是 updater 的唯一输入。包络依次包含四个大端 `uint32_t`：
`vendorID`、`productCode`、`revisionNumber`、`serialNumber`。镜像长度、版本和
均不需要内层描述符；gateway 不再重复计算接收或加载摘要：

| 事实 | 来源 |
| --- | --- |
| 本次接收长度 | Remote Handler `INIT:size`，不超过 16 + 131072 B |
| `image_size` | 剥离 16 B 身份包络后的实际 MCUboot 镜像长度 |
| 目标版本（判卷依据） | MCUboot header 固定偏移（`ih_magic==0x96F3B83D` 门禁后读 `ih_ver`），即 `major.minor.revision+build` 完整四段 |

Gateway 对 header 的读取是声明性读取，用于传输决策与升级判卷；header 布局、
TLV、trailer 与签名的最终校验均由 MCU 侧 MCUboot 执行。

SecurityAccess token 的目标身份来自包络，key ID 与版本来自
`shared/security_token_profile.h` 和 `profile.h`。

## 事务数据与结果确认

升级包只存在于 `mcu-updater` 持有的匿名文件描述符中，不建立路径，也不写入 Linux
持久化分区。事务结束、进程退出或网关断电时数据自动消失。

MCU 更新、重连和运行版本确认全部成功后才回复 handler ACK；任一步失败都回复
NACK。失败由 Suricatta 直接上报，成功时 SWUpdate 持久化 `ustate=1` 并重启，
启动脚本传入 `-c 2`，由 Suricatta 自行取得 action ID 并上报 `closed/success`。
Gateway 中断后的 MCU 整包重传未实现，详见
[运行说明](systemd-hawkbit-runtime.md)。

## 历史版本说明

v3 及之前的 `package-metadata.json` 描述符（`schema`/`release_id`/
`mcu_family`）没有不可替代职责：封闭世界解析替代 schema，hawkBit 制品已可
标识 release，而 MCU identity 是共享 token 契约。它已整体删除，不保留兼容壳。

v4 起内层 newc CPIO 也整体删除：v3 后它只包裹单一镜像，SWU 外层已经是权威
容器。正式服务没有发布参数、通用命令行选项或环境配置层。
