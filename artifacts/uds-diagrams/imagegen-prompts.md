# UDS diagrams — ImageGen prompt set

Generation mode: Codex built-in `image_gen` (`infographic-diagram`).

The diagrams are based on the current working tree, especially:

- `shared/uds_protocol.h`
- `shared/can_network.h`
- `gateway/src/profile.h`
- `gateway/src/uds/uds_client.c`
- `gateway/src/uds/uds_transaction.c`
- `gateway/src/ota/transfer.c`
- `can/uds/uds_server.c`
- `can/download/download.h`
- `can/docs/uds-can-profile.md`
- `gateway/docs/uds-mcu-alignment.md`

## Sheet 1 — Gateway / MCU session and timing

```text
Use case: infographic-diagram
Asset type: project technical architecture poster, sheet 1 of 2
Primary request: Create a precise, presentation-quality 16:9 landscape technical sequence diagram for the E:\T527\can_boot Gateway ↔ MCU UDS-over-Classic-CAN implementation. Show Gateway as Linux UDS client, SocketCAN/ISO-TP/Classic CAN as transport, and MCU as UDS server. Time flows top-to-bottom.
Style: clean white engineering-document background; crisp vector-like systems infographic; Gateway blue, transport cyan, MCU teal, timing orange, S3 purple, errors red; readable Chinese sans-serif and monospace protocol bytes.
Title: "Gateway ↔ MCU 双端 UDS 会话与时序"
Subtitle: "E:\T527\can_boot · ISO 14229 / ISO-TP · 项目实际实现"
Sequence:
1. "10 03  进入扩展会话" → "50 03 00 32 01 F4"
2. "10 02  进入编程会话" → "50 02 00 32 01 F4"
3. "27 01" → "67 01 <seed>"; "27 02 <token>" → "67 02"
4. "31 01 FF 00" → "71 01 FF 00"
5. Loop: "31 03 FF 00" → "未完成：7F 31 24"; "31 03 FF 00" → "完成：71 03 FF 00 00 00 00 00"; inside loop "每 1 s: 3E 00 ↔ 7E 00"
6. "34 00 44 <addr=0><size><SHA-256>" → "74 20 01 02 <target_slot>"
7. Repeat: "36 BSC <最多 256 B>" → "76 BSC"; immediately after success "3E 00 ↔ 7E 00"
8. "37" → "77"
9. "11 01" → "51 01"; note "P2/P2* 恢复默认值"
Timing callout:
"P2ServerMax = 50 ms"
"P2*ServerMax = 5000 ms"
"0x50 wire: P2=00 32; P2*=01 F4 × 10 ms"
"首响应先等 P2；仅收到 7F <SID> 78 后，下一次等待切换到 P2*"
"最多 8 个 0x78；单事务总预算 30 s"
"多帧请求：Gateway 等待预算额外计入 ISO-TP 发送耗时 + 3000 ms 裕量"
Optional branch:
"通用能力（当前 OTA 擦除不用 0x78）"
"7F <SID> 78  ResponsePending"
"擦除进度使用 31 FF00 结果轮询（未完成：7F 31 24；完成：71 03 FF 00 + 4-byte BE 状态记录）"
S3 callout:
"S3Server = 5100 ms"
"非默认会话"
"10 02/03 或有效 3E 00/80 → 重装 S3"
"普通业务请求不重装 S3"
"Δt ≥ 5100 ms → 默认会话 + 清除安全解锁"
"Gateway：准备轮询每 1 s 保活；每个 36 块后保活"
CAN strip:
"Classic CAN · 11-bit normal addressing · 500 kbit/s"
"请求 ID 0x7E0  →  MCU"
"响应 ID 0x7E8  →  Gateway"
"BS=8 · STmin=2 ms · CAN DLC=8"
Constraints: exact values and arrow directions; distinguish current OTA behavior from generic ResponsePending capability; no fictional services, logos, watermark, decoration, tiny text, or duplicate labels.
```

## Sheet 2 — 1 KiB packet transformation

```text
Use case: infographic-diagram
Asset type: project technical packet-format poster, sheet 2 of 2
Primary request: Create a precise 16:9 engineering infographic explaining the real E:\T527\can_boot conversion of exactly 1 KiB image data through UDS TransferData, ISO-TP, and Classic CAN. Show byte-accurate examples and frame counts.
Title: "1 KiB 原始数据 → UDS → ISO-TP → Classic CAN"
Subtitle: "项目约束：256 B/TransferData · MCU ISO-TP RX 缓冲区 512 B"
Pipeline:
"原始镜像数据"; "Image[0..1023] = 1024 B"
"Block #1: 36 01 D[0..255]       = 258 B"
"Block #2: 36 02 D[256..511]     = 258 B"
"Block #3: 36 03 D[512..767]     = 258 B"
"Block #4: 36 04 D[768..1023]    = 258 B"
"BSC 从 01 开始；每块含 SID + BSC + 256 B 数据"
Zoom title: "放大任意一块：BSC=k，局部数据 d0..d255"
UDS PDU: "36  k  d0  d1  ...  d255"; "UDS PDU LEN = 258 = 0x102"
Frame train:
"ID 0x7E0  FF   11 02 36 k d0 d1 d2 d3"
"ID 0x7E8  FC   30 08 02 00 00 00 00 00"
"ID 0x7E0  CF1  21 d4 d5 d6 d7 d8 d9 d10"
"ID 0x7E0  CF8  28 d53 ... d59"
"ID 0x7E8  FC   30 08 02 00 00 00 00 00"
"FC 初始 + CF8 + CF16 + CF24 + CF32 后，共 5 帧"
"CF15: 2F d102...d108"
"CF16: 20 d109...d115   ← SN 回卷"
"CF32: 20 d221...d227   ← SN 回卷"
"ID 0x7E0  CF36 24 d249 d250 d251 d252 d253 d254 d255"
"ID 0x7E8  ACK  02 76 k 00 00 00 00 00"
"BS=8：每 8 个 CF 等待新 FC"
"STmin=2 ms：相邻 CF 的最小间隔"
"每块：1 FF + 36 CF = 37 个请求 CAN 帧"
"每块成功后保活：02 3E 00  →  02 7E 00 00 00 00 00"
Counts:
"每块数据交换：37 请求 + 5 FC + 1 ACK = 43 帧"
"加 TesterPresent：43 + 2 = 45 帧/块"
"1 KiB 共 4 块：172 帧；含保活共 180 帧"
Timing:
"MCU：完整收到请求后，P2ServerMax = 50 ms"
"Gateway 多帧首响应等待预算：50 + 36×2 + 3000 = 3122 ms"
"收到 7F 36 78 后，下一次等待 P2*ServerMax = 5000 ms"
Warning:
"禁止：把 1024 B 作为单个 UDS PDU"
"原因：MCU ISO-TP RX 缓冲区仅 512 B"
"结果：FC(OVFLW)"
"正确：先在 UDS 层拆成 4 × 256 B"
Classic CAN anatomy:
"SOF | 11-bit ID | RTR | IDE | r0 | DLC | DATA[0..7] | CRC | CRC delimiter | ACK | EOF"
"1 | 11 | 1 | 1 | 1 | 4 | 64 | 15 | 1 | 2 | 7"
"总线传输：108 bit（不含位填充）"
"ISO-TP PCI 和 UDS 字节位于 DATA[0..7]"
Footer: "Classic CAN · 11-bit normal addressing · 500 kbit/s · 请求 0x7E0 · 响应 0x7E8"
Constraints: every byte and count accurate; correct directions; exactly 36 CF and five FC per 258-byte PDU; explicitly four UDS blocks; no CAN FD, 29-bit IDs, functional addressing, logos, watermark, decoration, or tiny text.
```

## Targeted correction applied to sheet 2

```text
Change only the bottom-left Classic CAN anatomy panel: add "CRC delimiter" with bit count 1 and change the unstuffed total from 107 bit to 108 bit. Keep all other pixels, protocol bytes, arrows, labels, and counts unchanged.
```
