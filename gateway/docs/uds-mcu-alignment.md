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

## S3 会话维持

- MCU S3Server 为 5100 ms。完整诊断请求处理完毕，且最终正/负响应发送完成后重启；
  suppressPositiveResponse 在服务完成后重启。异步 ISO-TP 响应发送期间不计入空闲 S3。
- 普通 `0x22/0x27/0x31/0x34/0x36/0x37` 请求和不支持 SID 的负响应均维持会话。
  S3 超时返回默认会话、清除解锁并终止下载事务（包括正在进行的擦除作业）。
- 连续传输及擦除轮询本身维持会话；`uds_tester_present()` 保留给真实空闲期。

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
`0x31 03 FF 00` 直到正响应携带成功记录 `0x00000000`（结果尚不可用时返回标准
requestSequenceError `0x24`；Flash 失败返回 NRC `0x72`），再发送标准 `0x34`：
`34 00 44 00 00 00 00 <size[4]>`，响应严格为 `74 20 01 02`。
Gateway 从升级前 active slot 推导期望 inactive slot，仅用于复位后验证；MCU 自行决定实际写入槽。
Gateway 从 `0x74` 的 maxNumberOfBlockLength 扣除 SID 与 BSC 两字节，并以本地 256 字节
发送上限分块；响应值不足 3 时拒绝传输。
本 profile 的单 DID RDBI、固定地址、256 字节 TransferData 上限、FF00 erase 和 token/signature
SecurityAccess 是产品策略，不改变 ISO 请求/响应结构。

相关 host tests 位于 `tests/test_uds_client.c` 和 `tests/test_isotp_channel.c`：它们覆盖 P2* wire
解码、会话协商时序、首个与重复 `0x78`、8 个 pending 上限、畸形 NRC，以及 ISO-TP Flow
Control 编码范围；`tests/test_transfer.c` 还覆盖下载准备例程到 `0x34` 的顺序。
