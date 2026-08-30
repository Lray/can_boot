# Gateway / ECU UDS 对齐契约

gateway 是 ECU 的 UDS client，不是第二个 UDS server。它不生成 `0x78`、`0x21` 或任何 ECU
正响应；它负责按 ECU profile 发送请求、完整接收 ISO-TP PDU，并以相同的事务语义解释 ECU
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

其中 P2* 在 wire 上以 10 ms 为单位编码；ECU 的 `5000 ms` 因而编码为 `0x01F4`。gateway 在
每次有效 `0x10` 后更新后续事务的 P2/P2*，而在 `0x11 01` 成功后恢复本 profile 的默认值。

每个服务都先按当前 P2 等待（多帧请求额外计入本端 ISO-TP 发包裕量）。gateway 不会因为服务
类型、长 token 或 Flash routine 而直接把首次等待放宽到 P2*。

## ResponsePending

- 只有长度正好为 3、SID 与原请求相同的 `7F <SID> 78` 才是 ResponsePending。
- 每个有效 `0x78` 仅把下一次等待切换到当前协商的 P2*；最终响应仍须是匹配的正响应或终态 NRC。
- gateway 最多接受 8 个 `0x78`；第 9 个返回 `UDS_ERR_RESPONSE_PENDING_LIMIT`，避免无限等待。
- `0x78` 是通用 UDS 事务能力，不承担下载擦除进度。下载准备由下述 `F001` 例程的结果轮询表达。

## 服务边界

客户端覆盖 ECU OTA profile 使用的 `0x10/0x11/0x22/0x27/0x31/0x34/0x36/0x37/0x3E`。普通业务
调用使用类型化 API；诊断/回归工具复用同一
P2/P2*、`0x78`、NRC 长度与事务上限逻辑。

下载的固定顺序是：`0x31 01 F0 01 <size><payload_id>` 启动准备，轮询
`0x31 03 F0 01` 直到结果为 `ready`，再发送扩展 `0x34`。`0x34` 仅绑定描述符、
返回目标槽与恢复偏移；它不会擦除 Flash 或返回 `0x78`。

相关 host tests 位于 `tests/test_uds_client.c` 和 `tests/test_isotp_channel.c`：它们覆盖 P2* wire
解码、会话协商时序、首个与重复 `0x78`、8 个 pending 上限、畸形 NRC，以及 ISO-TP Flow
Control 编码范围；`tests/test_resume_transfer.c` 还覆盖下载准备例程到 `0x34` 的顺序。
