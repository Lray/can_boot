# Gateway / MCU UDS 对齐契约

gateway 是 MCU 的 UDS client，不是第二个 UDS server。它不生成 `0x78`、`0x21` 或任何 MCU
正响应；它负责按 MCU profile 发送请求、完整接收 ISO-TP PDU，并以相同的事务语义解释 MCU
响应。

## 传输与寻址

- Classic CAN、11-bit normal addressing：请求 `0x7E0`，响应 `0x7E8`，500 kbit/s。
- Linux SocketCAN ISO-TP 接收 Flow Control 默认为 `BS=8`、`STmin=2 ms`。配置同时接受
  ISO 15765-2 允许的 `BS=0` 和 `STmin=0x00..0x7F`、`0xF1..0xF9` 编码。
- SocketCAN 将一个完整 ISO-TP PDU 交给 UDS client；若内核报告截断，UDS client 将其作为
  buffer-too-small 失败，而不会把截断数据解释为合法 UDS 响应。

## 会话与时序

默认时序仅用于首次 `0x10` 前：`P2ServerMax=50 ms`、`P2*ServerMax=5000 ms`。gateway 必须
接受且验证完整的 SessionControl 正响应：

`50 <session> <P2-hi> <P2-lo> <P2*-hi> <P2*-lo>`

其中 P2* 在 wire 上以 10 ms 为单位编码；MCU 的 `5000 ms` 因而编码为 `0x01F4`。gateway 在
每次有效 `0x10` 后更新后续事务的 P2/P2*，而在 `0x11 01` 成功后恢复本 profile 的默认值。

每个服务都先按当前 P2 等待（多帧请求额外计入本端 ISO-TP 发包裕量）。gateway 不会因为服务
类型、长 token 或 Flash routine 而直接把首次等待放宽到 P2*。

## S3 会话维持（对齐 ISO 14229-1 / iso14229 参考实现）

- MCU 端 S3Server 定时器 = `UDS_SERVER_DEFAULT_S3_MS`（5100 ms，ISO 14229-2 Table 5
  "5000 −0/+200 ms" 容差上限），仅由 `0x10` 进入非默认会话与 `0x3E`（子功能 0x00/0x80）
  重置；普通业务请求不重置。
- 到期检查在每轮 `UDS_PollS3` 无条件执行（响应传输中不暂停），超时后 MCU 回默认会话并清除
  安全解锁状态。
- gateway 承担 S3Client 保活义务：传输期每个 `0x36` 块后发送 `0x3E`；下载准备轮询期每
  `UDS_KEEPALIVE_INTERVAL_MS`（1 s）发送一次 `0x3E`。保活失败不中断流程，后续业务请求会
  以 NRC 暴露会话失效。

## ResponsePending

- 只有长度正好为 3、SID 与原请求相同的 `7F <SID> 78` 才是 ResponsePending。
- 每个有效 `0x78` 仅把下一次等待切换到当前协商的 P2*；最终响应仍须是匹配的正响应或终态 NRC。
- gateway 最多接受 8 个 `0x78`；第 9 个返回 `UDS_ERR_RESPONSE_PENDING_LIMIT`，避免无限等待。
- `0x78` 是通用 UDS 事务能力，不承担下载擦除进度。擦除进度由下述 `FF00` 例程的结果轮询表达。

## 服务边界

客户端覆盖 MCU update profile 使用的 `0x10/0x11/0x22/0x27/0x31/0x34/0x36/0x37/0x3E`。普通业务
调用使用类型化 API；诊断/回归工具复用同一
P2/P2*、`0x78`、NRC 长度与事务上限逻辑。

下载的固定顺序是：`0x31 01 FF 00` 启动 EraseMemory 例程（请求不带参数），轮询
`0x31 03 FF 00` 直到正响应携带 4 字节 BE 结果记录（未完成回 NRC `0x24`；记录
`0x00000000` 成功 / `0x00000072` 失败），再发送扩展 `0x34`。`0x34` 绑定描述符
（payload SHA-256 与大小）、校验大小适配目标槽并返回目标槽；它不会擦除 Flash
或返回 `0x78`。

相关 host tests 位于 `tests/test_uds_client.c` 和 `tests/test_isotp_channel.c`：它们覆盖 P2* wire
解码、会话协商时序、首个与重复 `0x78`、8 个 pending 上限、畸形 NRC，以及 ISO-TP Flow
Control 编码范围；`tests/test_transfer.c` 还覆盖下载准备例程到 `0x34` 的顺序。
