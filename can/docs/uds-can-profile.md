# ECU UDS over CAN profile

本目录的 ECU 实现按 ISO 14229-3 的 UDS over CAN 应用边界和 ISO 15765-2
的 DoCAN 网络层规则实现。它是面向 OTA 引导程序的受限产品 profile，不宣称
覆盖 ISO 14229 全部服务或 ISO 15765-2 的全部寻址/链路变体。

## 当前实现

- ISO-TP 状态机直接采用 [`SimonCahill/isotp-c` v1.8.0](../ThirdParty/isotp-c/UPSTREAM.md)，
  对应提交 `abb9e552df0e7ca0148c146124795341d57124fe`；第三方运行时源码不作修改，
  STM32 适配仅实现 CAN 发送、毫秒分辨率的微秒单位时钟和调试三个上游端口函数；
  接收帧直接交给上游 `isotp_on_can_message()`。
- Classic CAN，8-byte CAN DLC，标准 11-bit normal addressing。
- 物理请求 ID 为 `0x7E0`，响应 ID 为 `0x7E8`；当前不接收 functional addressing、
  29-bit addressing、extended/mixed addressing 或 CAN FD。
- ISO-TP 支持 Single Frame、First/Consecutive Frame 和 Flow Control；接收端对
  超出 512-byte 配置缓冲区的 First Frame 返回 `FC(OVFLW)`。
- First Frame 接受普通 12-bit 长度；能够识别 32-bit extended length 格式，但所有
  合法 extended-length PDU 都超过 512-byte ECU 缓冲区，因此以 `FC(OVFLW)` 拒绝。
- 接收端 Flow Control 为 `CTS, BS=8, STmin=2 ms`；Flow Control 的解析和状态
  迁移完全由上游实现维护。上游默认最多接受 1 个连续 `WAIT`，超限后以
  `WFT_OVRN` 结束发送。
- `IsoTpLink` 独立维护发送与接收状态。发送 First Frame 后启动 `N_Bs=1 s`，
  接收 First Frame 或未完成块后启动/重装 `N_Cr=1 s`。FDCAN 忙或 FIFO 满时，
  STM32 端口严格返回上游定义的 `ISOTP_RET_NOSPACE`，不另建发送事务。时间基准
  使用 `HAL_GetTick() * 1000U`，返回值单位为微秒、分辨率为 1 ms；亚毫秒 STmin
  因 RTOS 轮询周期而向上取整，不宣称微秒级发送调度。
- 诊断服务覆盖当前 OTA profile 使用的 `0x10/0x11/0x22/0x27/0x31/0x34/0x36/
  0x37/0x3E`。支持带 sub-function 的服务的 suppress-positive-response bit，
  只抑制正响应，负响应仍正常返回。
- `0x10` 正响应公布 `P2ServerMax=50 ms` 和 `P2*ServerMax=5000 ms`；后者按
  ISO 14229 的 10 ms wire unit 编码为 `0x01F4`。
- `0x34 RequestDownload` 是产品扩展：标准 address/size 字段后追加完整 payload
  SHA-256；正响应在最大块长后追加 ECU 选定的 `target_slot` 与唯一可恢复的
  `resume_offset`。地址必须为零，目标槽始终由 ECU 的 inactive slot 推导。
- `0x31 StartRoutine F001` 是下载准备的产品例程：请求携带完整镜像大小和
  payload SHA-256，启动独立的擦除作业；`0x31 RequestRoutineResults F001` 返回
  `pending` 或 `ready`。相同描述符的重复启动只返回当前作业，不重复擦除。擦除每轮
  最多处理一个 8 KiB Flash page，由 OTA 组合根轮询。
- `0x34 RequestDownload` 只验证已经 `ready` 的同一下载描述符，并立即返回 `0x74`；
  它不擦除 Flash，也不发送 `0x78`。
- `DID_CONFIRM_RESULT (0xF1A8)` 仅报告当前启动的自检与 MCUboot `image_ok`
  写入结果；它不是持久化激活状态。
- `DID_APP_VERSION` 使用 8 字节完整 MCUboot 版本：`major, minor, revision
  (BE16), build (BE32)`。

## 明确的产品边界

擦除属于 `F001` 准备例程，而不是数据传输服务。`0x36 TransferData` 单次写入仍受
`DOWNLOAD_MAX_TRANSFER_PAYLOAD`（256 bytes）约束。普通 OTA 镜像由制作工具
预置 pending magic；最后一个 TransferData 只编程独立的 16-byte magic 单元。
镜像完整性验真（SHA-256、签名、TLV、安全计数器）在复位后由 MCUboot 完成。
板级集成必须证明单页擦除和单次写入在声明的 P2 时限内完成。擦除持续时间由例程
结果轮询表达，而不是以更长的 P2* 或 `0x78` 隐藏在 `0x34` 中。

同样，若产品需要 functional addressing、29-bit/CAN FD 或更大的 UDS PDU，必须
先扩展 `can_frame_t`、CAN filter、ISO-TP addressing/length/padding 状态机及对应
测试，不能仅修改 ID 常量后宣称符合相应 profile。

## 回归覆盖

ISO-TP 主机测试直接调用上游 API，覆盖无填充 Single Frame、多帧接收、序号错误、
接收缓冲区 Overflow、发送填充、BS/STmin 发送节拍以及 `N_Bs/N_Cr` 超时原因。
UDS/下载测试覆盖正响应抑制、P2* wire 编码、下载准备作业的 `pending`、`ready`
以及 `0x34` 仅在 ready 后接受。固定寻址及 Classic CAN 限制由
`can_network.h`、`can_frame.h` 和本文件共同定义。
