# ECU OTA 包格式

OTA worker 的 package 只有一个成员：

| 成员 | 唯一职责 |
| --- | --- |
| `image.bin` | MCUboot 全槽签名镜像本体 |

成员与目录名称由 `src/package/package_input.h` 定义（`image.bin` /
`package-input-v1.partial` / `package-input-v1`）；worker 与 SWUpdate
adapter 只依赖这个包输入契约，不互相依赖。

## 传输、授权与发布

SWU 的唯一 remote artifact 是 `image.bin`（`type = "remote"`），经
Remote Handler ZMQ 通道到达 gateway。Remote Handler 是流式协议：SWUpdate
逐块等待 bridge 的 `ACK`，然后才在传输结束后完成自己的 image sha256
校验。因此 bridge 在最后一个 `DATA` 的 `ACK` 前不能把 SWUpdate 的成功校验
当作既成事实。

每次 orchestrator 启动必须传入受发布流程控制的
`--expected-size` 和 `--expected-sha256`。它们绑定本次允许传输的原始
`image.bin`，不是内层包文件，也不会交给 worker。`package_store` 要求
`INIT` 尺寸相等、流式计算 SHA-256、只在摘要相等时依序 `fsync` 文件、改名
`image.bin`、`fsync` 目录、原子改名目录为 `package-input-v1` 并 `fsync`
父目录。任何尺寸或摘要不符都不会发布文件或启动 worker。

接收上限等于 `DEFAULT_SLOT_SIZE`（当前 131072 B），与 worker 的镜像加载上限
一致。SWUpdate 仍负责验证 SWU 外层的签名与描述中的 hash；ECU 的 MCUboot
验签、security counter、slot 选择与回滚仍是最终安全裁决。

## 字段来源与推导规则

`image.bin` 是 worker 的唯一输入。镜像长度、版本和摘要不需要内层描述符：

| 事实 | 来源 |
| --- | --- |
| `image_size` | `image.bin` 文件长度 |
| `image_sha256`（续传 payload_id） | worker 加载时对镜像整体计算 SHA-256 |
| 目标版本（判卷依据） | MCUboot header 固定偏移（`ih_magic==0x96F3B83D` 门禁后读 `ih_ver`），即 `major.minor.revision+build` 完整四段 |

Gateway 对 header 的读取是声明性读取，用于传输决策与升级判卷；header 布局、
TLV、trailer 与签名的最终校验均由 ECU 侧 MCUboot 执行。

SecurityAccess token 的设备身份、key ID 与版本来自
`shared/security_token_profile.h` 和 `profile.h`，不属于包内容。

## 历史版本说明

v3 及之前的 `package-metadata.json` 描述符（`schema`/`release_id`/
`ecu_family`）没有不可替代职责：封闭世界解析替代 schema，hawkBit 制品已可
标识 release，而 ECU identity 是共享 token 契约。它已整体删除，不保留兼容壳。

v4 起内层 newc CPIO 也整体删除：v3 后它只包裹单一镜像，SWU 外层已经是权威
容器。恢复的 `--expected-size`/`--expected-sha256` 不是旧 metadata 的复活，
而是 Remote Handler 流式顺序所需的、由发布流程提供的发布前内容绑定。
