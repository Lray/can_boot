# MCU update 包格式

单 MCU updater 的 package 只有一个成员：

| 成员 | 唯一职责 |
| --- | --- |
| `image.bin` | MCUboot 全槽签名镜像本体 |

成员与目录名称由 `src/package/package_input.h` 定义（`image.bin` /
`package-input-v1.partial` / `package-input-v1`）；updater 与 SWUpdate
adapter 只依赖这个包输入契约，不互相依赖。

## 传输、授权与发布

SWU 的唯一 remote artifact 是 `image.bin`（`type = "remote"`），经
Remote Handler ZMQ 通道到达 gateway。生产集成要求 SWUpdate 使用现有的
“先验证再转发”行为：完整暂存 SWU artifact，完成 SWU 签名和描述中 artifact
hash 的校验后，才连接本机 Remote Handler 接收端并发送 `INIT` / `DATA`。
因此，只有已经通过 SWUpdate 发布授权校验的字节能够进入接收端。

`mcu-updater` 是与具体发布无关的常驻服务，不接受发布侧的 job ID、期望长度或
期望摘要。每次事务由接收端生成内部 UUID；`package_store` 从官方 `INIT:size`
获取本次声明长度，流式接收并派生实际 SHA-256，在字节数精确相等时依序
`fsync` 文件、改名 `image.bin`、`fsync` 目录、原子改名目录为
`package-input-v1` 并 `fsync` 父目录。尺寸越界、数据超量、提前结束或存储失败
都不会启动 MCU 更新。

接收上限等于 `DEFAULT_SLOT_SIZE`（当前 131072 B），与 updater 的镜像加载上限
一致。SWUpdate 负责验证 SWU 外层的签名与描述中的 artifact hash；本机
Remote Handler endpoint 仅允许受控的 SWUpdate 进程访问；MCU 的 MCUboot
验签、security counter、slot 选择与回滚仍是最终安全裁决。

本项目不启用 `hardware-compatibility`。产品当前只有一个固定 MCU 目标，镜像
适配约束由签名发布流程、MCUboot header 读取和 MCU 侧最终校验共同承担。

## 字段来源与推导规则

`image.bin` 是 updater 的唯一输入。镜像长度、版本和摘要不需要内层描述符：

| 事实 | 来源 |
| --- | --- |
| 本次接收长度 | Remote Handler `INIT:size`，并受 `DEFAULT_SLOT_SIZE` 上限约束 |
| 内部事务 ID | `mcu-updater` 在接收 `INIT` 后生成的 UUID；不来自发布配置 |
| `image_size` | `image.bin` 文件长度 |
| 接收审计摘要 | `package_store` 对收到的 `image.bin` 流式计算 SHA-256 |
| `image_sha256`（续传 payload_id） | updater 打开已发布文件后独立计算 SHA-256 |
| 目标版本（判卷依据） | MCUboot header 固定偏移（`ih_magic==0x96F3B83D` 门禁后读 `ih_ver`），即 `major.minor.revision+build` 完整四段 |

Gateway 对 header 的读取是声明性读取，用于传输决策与升级判卷；header 布局、
TLV、trailer 与签名的最终校验均由 MCU 侧 MCUboot 执行。

SecurityAccess token 的设备身份、key ID 与版本来自
`shared/security_token_profile.h` 和 `profile.h`，不属于包内容。

## 历史版本说明

v3 及之前的 `package-metadata.json` 描述符（`schema`/`release_id`/
`mcu_family`）没有不可替代职责：封闭世界解析替代 schema，hawkBit 制品已可
标识 release，而 MCU identity 是共享 token 契约。它已整体删除，不保留兼容壳。

v4 起内层 newc CPIO 也整体删除：v3 后它只包裹单一镜像，SWU 外层已经是权威
容器。早期过渡实现曾把期望长度、期望摘要和 job ID 写入 systemd 环境；该做法
会把正式服务绑定到某次发布，现已删除，不属于当前接口，也不保留兼容参数。
