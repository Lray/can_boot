# Gateway 函数职责与重复实现审查嫌疑清单（含复核结论）

> 审查快照：2026-08-12
> 复核快照：2026-08-12
>
> 初始复核结论（共 53 条）：**48 条 ✅ 成立、4 条 ⚠️ 部分成立、1 条降级为非问题、0 条推翻**。
> 本轮按 `docs/superpowers/specs/2026-08-12-gateway-responsibility-refactor-design.md` 完成了
> P0-P5 中所有可执行代码项；下方历史表保留初始嫌疑与证据，最新状态以本节为准。
>
> 标记约定：✅ = 成立；⚠️ = 部分成立 / 需修正表述；❌ = 不成立（本轮无）。
> 本文同时记录历史嫌疑、修复映射和验证结果；“成立”表示初始嫌疑成立，不表示当前代码仍未修复。
>
> 表中的行号来自审查快照（复核后多数行号已修正）。代码继续变更后，应以函数名和文件路径重新定位。

## 最新修复状态（2026-08-12）

| 优先级 | 修复项 | 当前状态 |
|---|---|---|
| P0 | `secure_zero`、`is_lower_hex`、`is_uuid_v4`、`valid_ifname` 抽到 `src/util/` | ✅ 已完成；生产调用方不再保留重复实现 |
| P1 | OTA snapshot 观察、`pending_resume_stage`、`fail()` 状态、routine wrapper、具名 transport error、默认 sleep、resume 溢出 | ✅ 已完成；snapshot 与 executor 通过共享的 `OtaRuntimeDeps_t` 传递运行依赖，状态失败点改为最后完成状态 |
| P2 | manifest 文件 I/O/薄封装删除、JSON 拆分、image hash 单次计算、MCUboot 命名/字段/边界常量 | ✅ 已完成；`package_metadata` 仅编排加载、绑定与释放 |
| P3 | UDS 事务引擎拆分、observer 删除、session 双校验、SecurityAccess P2* 特殊放宽、raw DID 薄封装、invalid-arg 语义、SecurityAccess context | ✅ 已完成；通用交易逻辑集中在 `src/uds/uds_transaction.*` |
| P4 | bundle 名称契约、文件工具、bridge framing、bridge/worker CLI 拆分、ECU deployment profile、session 状态镜像 | ✅ 已完成；bridge 与 probe 共用 framing，worker 与 probe 共用部署常量 |
| P5 | token signer codec/跨语言协议契约拆分、SocketCAN CLI/默认 open 删除、`recv` 返回契约、`stmin_raw`、CMake 死配置与 hash 重复编译 | ✅ 已完成；`gateway-hash` 和 `gateway-bridge-framing` 已纳入构建图 |
| P6 | SecurityAccess、MCUboot、RequestDownload、OTA executor 公共结果 API 收窄 | ✅ 已完成；长度结果改为内部数据，Gateway 不再解析 MCUboot，`EcuImageIdentity_t` 已平铺到 manifest，RequestDownload 仅暴露 Gateway 消费字段，executor 内部状态不再混入公共结果 |

### 当前有意保留的项目

- `adapters/swupdate/hawkbit.*`：预留 SWUpdate adapter，按已确认决策不改动。
- `gateway-core`：保持单一静态库边界，避免为低置信度嫌疑制造新的链接层。
- `uds_client_request_raw()`：保留为原始报文诊断/回归入口；已删除无调用的
  `uds_read_did_raw()`、`socketcan_raw_open()` 及 SocketCAN hex parser。
- `scripts/coding_quality_config.json`：新增 adapter/app 文件尚未纳入依赖检查覆盖；
  该设计要求已在本轮复审中确认，但按当前明确决策暂不处理。
- 受真实 ECU、Flash、MCUboot 选择/回滚影响的验收仍需板端/HIL，host 静态检查不替代硬件证据。

### 当前验证结果

- `scripts/dependency_check.py`：扫描 56 个文件，反向依赖问题 0 条。
- `tests/test_coding_quality_tools.py`：28/28 通过。
- 核心 C 源码与相关 host 测试已通过 Clang 严格语法检查；Linux-only bridge、SocketCAN、token signer
  文件仍需 Linux 头文件/工具链验证。
- 完整 CMake/CTest 在当前 Windows Espressif 工具链下无法完成链接（工具链拒绝 Windows PE
  版本参数并缺少 `kernel32`），不是本轮源码断言为通过的测试。

## 复核摘要（跨条目结论）

1. **死 API 面**：已按决策删除 `uds_read_did_raw`、`socketcan_raw_open`、SocketCAN hex parser、
   `package_manifest_load_file`、`package_validate_image_file`、`package_image_sha256`；保留原始报文
   入口 `uds_client_request_raw` 和预留 hawkbit adapter。
2. **文档矛盾**：已删除 SecurityAccess 的首个 P2* 特殊放宽，`docs/uds-ecu-alignment.md` 与实现统一为
   当前 P2/P2* 事务策略。
3. `pending_resume_stage`：已删除字段、赋值点和 worker 输出，stage 4 死状态不再存在。
4. `ota_bridge_session`：已允许 COMPLETE→FAILED，staging/worker 失败时由主循环调用 abort，状态不再只靠进程退出码表达。
5. `UDS_ERR_RESPONSE_PENDING_LIMIT(-7)`：已由 `uds_is_transport_error()` 统一分类，避免落入裸数字区间判断。

## 历史嫌疑表（初始快照；不表示当前代码未修复）

以下表格的“嫌疑”与“复核结论”保留审查证据。若表内历史行号、旧函数名或旧实现与上方最新修复状态冲突，
以当前源码和“最新修复状态”章节为准。

### 高优先级嫌疑

| 级别 | 嫌疑 | 证据 | 复核重点 | 复核结论 |
|---|---|---|---|---|
| 高 | `uds_client.c` 同时承担事务引擎、超时协商、Session 状态、observer、raw API 和全部 typed service codec | `gateway/src/uds/uds_client.c:102`、`:281`、`:384` | 是否仍属于单一"UDS client"职责，还是已经变成协议服务总包 | ✅ 成立。878 行、31 个函数：交易引擎（:89-266）、observer（:60-87、:283-323）、raw API、约 10 个 typed codec、Session/P2 状态均在同一文件。"协议服务总包"判断属实，是否拆分属设计决策 |
| 高 | Session response 被校验两次 | `gateway/src/uds/uds_client.c:344`、`:438` | 通用事务 validator 已校验一次，`uds_client_accept_session_control_response()` 又校验一次 | ✅ 成立，行号修正：第二处校验在 `:358`（:438 是 `uds_tester_present` 内部）。同一 `validate_session_control_response` 对相同字节执行两次（:428 传入、:358 再跑）；:365-367 的 P2 采纳是第二遍的附加功能，纯校验部分确实冗余 |
| 高 | raw DID API 层次重叠，且 `uds_read_did_raw()` 当前未发现调用点 | `gateway/src/uds/uds_client.h:159`、`:187`；`gateway/src/uds/uds_client.c:549` | `request_raw`、`read_did_exchange`、`read_did_raw`、`read_did` 是否需要同时公开 | ✅ 成立。`uds_read_did_raw` 全仓零调用（仅定义 :536 与声明 h:187）；`uds_client_request_raw` 仅 `tests/test_uds_client.c:230` 使用；`uds_read_did` 是生产主力（`ota_executor.c` 约 12 处）。`docs/uds-ecu-alignment.md:40` 声称的 "board-level tools" 在仓库中不存在 |
| 高 | SecurityAccess 首次超时策略分裂 | `gateway/src/uds/uds_client.c:404`、`:515` | raw API 对 SecurityAccess 特殊放宽到 P2*，typed `uds_security_send_token()` 却走普通 P2 | ✅ 成立，行号修正：放宽逻辑在 `:396-400`（:404 是引擎调用尾部）。typed `uds_security_send_token`（:515）确实走普通 P2。**额外发现**：与 `docs/uds-ecu-alignment.md:25-26` 文档化策略直接矛盾（见复核摘要 2） |
| 高 | manifest 同时提供文件加载、字节解析、buffer 校验、file 校验，多条路径疑似重复 | `gateway/src/package/package_manifest.h:37`；`gateway/src/package/package_manifest.c:113`、`:157` | `package_manifest_load_file()`、`package_validate_image_file()` 当前没有生产调用点 | ✅ 成立，行号修正：头文件证据在 `h:41-45`（:37 是 `package_image_sha256`）。`package_manifest_load_file`（c:113）与 `package_validate_image_file`（c:157）全仓零生产调用；生产（`package_metadata.c:393/558/566`）走自己的 `read_regular_file` + buffer 变体 |
| 高 | `package_manifest.c` 混合序列化、CRC、manifest 校验和 image hash 校验 | `gateway/src/package/package_manifest.c` | 是否应拆成 wire codec、结构校验、文件读取三部分 | ✅ 已收敛：文件 I/O 未进入 manifest 模块，CRC 计算为私有实现；公共路径只保留 `parse_bytes`、`validate`、`validate_image_buffer` |
| 高 | `package_metadata.c` 同时承担 JSON parser、schema、身份绑定、hash、文件装载、MCUboot 校验和内存释放 | `gateway/src/package/package_metadata.c` | 是否将 MCUboot 解析/校验留给 bootloader | ✅ 已处理。Gateway 只加载文件并校验 manifest、包身份、CRC32 和全镜像 SHA-256 绑定；MCUboot header、TLV、trailer、签名和版本校验均已删除并由 bootloader 负责。 |
| 高 | image SHA256 可能被重复计算 | `gateway/src/package/package_manifest.c`；`gateway/src/package/package_metadata.c` | 第二次校验可能有绑定意义，但可以考虑一次计算、多个比较 | ✅ 已收敛：镜像摘要只在 `package_validate_image_buffer()` 中计算；manifest 摘要用于 JSON 与二进制 manifest 的绑定。 |
| 高 | `mcuboot_image_validate()` 名称容易让人误解为完成了真实性或签名验证 | 原 `gateway/src/package/mcuboot_image.*` | 是否保留 Gateway 侧 MCUboot layout/TLV/trailer 校验 | ✅ 已删除。Gateway 不再包含 MCUboot 镜像解析或校验逻辑，ECU bootloader 是唯一的 MCUboot 校验权威。 |
| 高 | 包内文件名契约在三个模块重复定义 | `gateway/adapters/swupdate/inner_bundle.h:13`；`gateway/apps/ota_worker/ota_worker.c:21`；`gateway/adapters/swupdate/ota_bridge.c:34` | 修改 bundle 文件名可能只改到一处，造成 bridge/worker 不一致 | ⚠️ 部分成立。成员文件名（`package-metadata-v2.json`/`ecu-manifest.bin`/`ecu-image.bin`）实际只在 `inner_bundle.h` 与 worker 输入路径中出现；bridge 复用 `INNER_BUNDLE_*` 宏。真正跨 worker+bridge 重复的是 `gateway-input-v1`/`.partial` 目录名 |
| 高 | `is_lower_hex()`、`is_uuid_v4()`、ifname 校验在 metadata、worker、bridge 重复实现 | `gateway/src/package/package_metadata.c:280`；`gateway/apps/ota_worker/ota_worker.c:59`；`gateway/adapters/swupdate/ota_bridge.c:79` | 三份实现的 null 处理和命名已经不完全一致 | ✅ 成立。"三处三份"表述过强：`is_uuid_v4` 三份逐字节一致（metadata :318 / worker :78 / bridge :94）；`is_lower_hex` 的 bridge 版（:79-93）**无 NULL 保护**且用 `text[length]=='\0'` 代替 strlen，行为等价但实现不一致；ifname 校验仅 worker（:105）+ bridge（:178）两份，package_metadata 无此校验。**已处理**：2026-08-12 合并到 `src/util/util.c`（`is_lower_hex`/`is_uuid_v4`/`valid_ifname`），四份本地 static 全部删除，语义以 NULL 保护 + 长度检查版本为准 |
| 高 | `ota_executor.c` 混合 snapshot、resume identity、时序、重试、routine policy、reset 和最终分类 | `gateway/src/ota/ota_executor.c:65`、`:150`、`:311` | 仍是 OTA 编排者，但内部子职责明显偏多 | ✅ 成立。13 个函数覆盖 6 个可分离关注点：clock/sleep 注入（:21-58）、snapshot 轮询重试（:60-143）、resume 身份（:145-232）、routine policy wrapper（:243-273）、reset/reconnect、分类（:275-305）。**已处理**：2026-08-12 clock/sleep 注入与 snapshot 轮询（`read_snapshot_once`/`snapshots_equal`/`collect_stable_snapshot`）迁至新增 `src/ota/ota_snapshot.c/.h`；routine policy wrapper 合并为参数化 `run_routine()` helper。**进一步处理**：executor 与 snapshot 改为共享 `src/ota/ota_runtime.h` 的 `OtaRuntimeDeps_t`，不再通过两份同字段配置结构体复制客户端、重连与时钟依赖 |
| 高 | OTA 结果同时用 `result_code`、`operation_rc`、`outcome` 与 `last_state` 表达状态 | `gateway/src/ota/ota_executor.h` | 状态轴过多，容易出现组合不一致 | **2026-08-20 收敛**：已删除自定义 pending/activation 生命周期和相应 boolean；`last_state` 只记录本次已完成的传输、复位、重连与观测步骤。`result_code` 是终态分类，`operation_rc` 是底层失败码。 |
| 高 | `pending_resume_stage` 使用裸数字且跳过 stage 4；`last_state` 还可能表达"观测到"而非"本次执行过" | `gateway/src/ota/ota_executor.c:165`、`:222`、`:384` | 建议重点确认字段语义和状态迁移 | ✅ 成立，且比文档更严重：`pending_resume_stage` 全仓**只写不读**（仅 :166/:207/:218/:229 四处赋值 1/2/3/5，无任何读取点），stage 4 从未赋值，可视为死状态。`last_state` 语义属实：:382 的 MARKED_PENDING 仅凭持久 DID 推断（mark 例程并未本次执行），:431 为显式 either/or。**已处理**：2026-08-12 `pending_resume_stage` 字段（含头文件声明、4 处赋值、worker 输出）全部删除 |
| 高 | bridge 主循环和 `ota_bridge_session` 各自维护一套状态 | `gateway/adapters/swupdate/ota_bridge_session.h:22`；`gateway/adapters/swupdate/ota_bridge.c:634` | `initialized`、`complete`、`exit_code` 与 session state 有重叠 | ✅ 成立（冗余而非冲突）。`initialized`（:652）镜像 `state!=NEW`，`complete`（:639）镜像 `state==COMPLETE`，`exit_code` 无 session 对应物；当前状态一致，但主循环双轨追踪无必要 |
| 高 | session 在 bundle 完成后就标记 COMPLETE，但后续 staging/worker 仍可能失败 | `gateway/adapters/swupdate/ota_bridge_session.c:216`；`gateway/adapters/swupdate/ota_bridge.c:677`、`:714` | 需要明确"上传完成"和"整个 job 完成"的所有权 | ✅ 成立，且更严重：COMPLETE 设置于 :208-225（rename 后），staging（:677）/worker（:683）在其后；`ota_bridge_session_abort` 对 COMPLETE 会话是 **no-op**（session.c:230），后续失败无法在 session 状态表达，仅靠进程退出码传递。"COMPLETE"实际含义是"传输完成"而非"任务成功" |
| 高 | `download` 曾没有 terminal/exited 状态 | `gateway/src/ota/download.c:35`、`:68` | `0x37` 成功后仍可能继续发送 block 或再次 exit | ✅ 已处理。`download` 在 0x37 成功后进入 terminal 态，后续 block/exit 都被拒绝 |
| 高 | `TokenRequest_t` 作为 `request_context` 传入，但 seed、level 等字段随后全部被覆盖 | `gateway/src/security/security_access.h:13`；`gateway/src/security/security_access.c:43` | API 语义应明确是"身份上下文"还是完整 token request | ✅ 成立。`security_access.c:43-46` 复制后仅 `identity` 存活，`security_level`/`seed`/`seed_len` 被覆盖；头文件无"身份上下文"语义说明；生产调用方（`ota_executor.c:370-373`）零初始化后仅设 `identity`，靠约定工作 |

## 中优先级嫌疑

| 嫌疑 | 证据 | 复核重点 | 复核结论 |
|---|---|---|---|
| `resume_transfer_execute()` 同时做 identity、remaining size、`0x34/0x36/0x37` transfer | `gateway/src/ota/resume_transfer.c` | 与 `ota_executor_run()` 的流程边界是否足够清楚 | ✅ 边界保留：executor 拥有 snapshot/session/security/reset/分类，resume transfer 只做 RequestDownload 响应校验、剩余长度校验和传输。身份与续传 cursor 均由扩展 `0x34` 一次协商 |
| resume transfer 与 download 重复维护 offset、size、sequence、transfer 结果 | `gateway/src/ota/download.h`；`gateway/src/ota/resume_transfer.h` | 中间计划和结果是否必需 | ✅ 已处理：删除 `ResumeTransferPlan_t` 和 `ResumeTransferResult_t`；剩余长度只在 helper 内计算，成功传输只向 executor 返回目标槽，传输字节和块序号只由 `DownloadSession_t` 维护 |
| OTA executor 的 precheck/mark wrapper 结构高度相似 | `gateway/src/ota/ota_executor.c:247`、`:266` | 是否保留两个 policy wrapper，或抽出通用 routine helper | ✅ 成立。两个 wrapper（:243-260 / :262-273）结构几乎相同（`routine_result` + rc 直返 + 结果码映射），唯一差异是 precheck 对 `UDS_ERR_TIMEOUT` 重试一次（:249-253）。可抽参数化 helper。**已处理**：2026-08-12 抽出 `run_routine()`（函数指针 + 超时重试标志 + 结果错误码参数化），两个 wrapper 删除 |
| OTA 使用可注入 clock，UDS 使用真实 monotonic clock | `gateway/src/ota/ota_executor.c:20`；`gateway/src/uds/uds_client.c:15` | 测试中的时间源并不统一 | ✅ 成立（行号偏移：executor 时钟在 :21-31/:45-58，uds 在 :16-25）。UDS 测试无法注入时间，只能真实等待 |
| executor 用错误码数值范围判断 transport error | `gateway/src/ota/ota_executor.c:396` | 依赖不同模块错误码连续排列，语义耦合较隐蔽 | ✅ 成立，且发现缺口：条件在 :392-394，区间 `[-6,-1]`；`UDS_ERR_RESPONSE_PENDING_LIMIT(-7)` 落在区间外，会被误分类为 `OTA_RESULT_TRANSFER_RESUME` 而非 TRANSPORT（见复核摘要 5）。**已处理**：2026-08-12 `uds_client.h/.c` 新增具名 `uds_is_transport_error()`（覆盖 -7），executor 数字区间判断替换 |
| `131072`、`131008` 等容量在 profile、MCUboot、metadata、worker、probe 多处出现 | 原 `gateway/src/profile.h` 与 `gateway/src/package/mcuboot_image.c` | 修改 slot/image 上限时可能漏改 | ✅ 已处理。Gateway 只保留传输所需的 `DEFAULT_SLOT_SIZE`；trailer 和 signed extent 的 MCUboot 容量边界已随 Gateway 解析器删除。 |
| worker 与 package probe 重复 ECU family、slot size policy | `gateway/apps/ota_worker/ota_worker.c`；`gateway/apps/ota_worker/ota_package_probe.c` | 是否应有统一 deployment profile | ✅ 已收敛。manifest slot-size 配置已删除；两者只传入固定 ECU family。 |
| `InnerBundleResult_t` 在生产 bridge 中只作为输出参数传入，字段未使用 | `gateway/adapters/swupdate/ota_bridge.c:450`；`gateway/adapters/swupdate/inner_bundle.h:17` | 是有意保留的诊断结果，还是多余 API | ✅ 成立，但不能简单删参：字段（metadata_size/manifest_size/image_size）确实计算后丢弃（:452-468），但 `inner_bundle_extract` 拒绝 `result_out==NULL`（inner_bundle.c:269），且测试依赖该结果。可改为可选参数或消费日志 |
| bridge 与 probe 重复实现 ZeroMQ request/response、ACK/NACK、INIT/DATA 协议 | `gateway/adapters/swupdate/ota_bridge.c:367`；`gateway/apps/ota_worker/ota_bridge_probe.c:13` | 工具和生产 adapter 是否应共享 framing 层 | ✅ 成立。双帧消息（command+body）、`INIT:<size>`/DATA 命令、ACK[:size]/NACK 回复、REP/REQ socket 对完全一致（bridge :367-372/:659-700 vs probe :13-28/:65-97）。probe 对非 ACK 一律判失败，不区分 NACK 语义 |
| token signer client 同时负责 CBOR 编解码、随机数、Unix socket、路径权限、peer credential 和 IO | `gateway/adapters/token_signer/token_signer_client.c:42`、`:263` | codec 与 transport/security policy 是否应分开 | ✅ 成立。400 行单文件覆盖 CBOR 编解码（:42-226）、getrandom（:93）、socket 路径权限（:263）、SO_PEERCRED/connect/send/recv（:277/:340） |
| C token signer 与 Python 开发 signer 重复维护 CBOR 协议、request/response size 等常量 | `gateway/adapters/token_signer/token_signer_codec.h`；`gateway/scripts/token_signer_v1.py` | 跨语言契约没有单一来源 | ✅ 已处理：2026-08-12 新增 `adapters/token_signer/token_signer_protocol_v1.def` 作为唯一契约；C 侧通过 `token_signer_protocol.h/.c` 编译为 profile、状态码和容量，Python signer 通过 `scripts/token_signer_protocol.py` 读取同一文件。原条目中的 Unix endpoint/proxy 路径不适用于当前仓库：Python 工具使用 TCP 开发连接，C 客户端的 Unix endpoint 为本地运行时配置。 |
| `UdsClient` public struct 暴露 observer 链表、notification depth 等实现细节 | `gateway/src/uds/uds_client.h:45` | 是否需要 opaque client | ✅ 成立。h:45-56 完全公开：`exchange_subscriptions`（observer 链表）、`exchange_notification_depth`（递归保护）等内部字段裸露 |
| UDS invalid argument 被映射为 `MALFORMED_RESPONSE` 或 `BUFFER_TOO_SMALL` | `gateway/src/uds/uds_client.h:10`；`gateway/src/uds/uds_client.c:523` | 错误分类不够清晰 | ✅ 成立（行号修正：映射在 `:510-513`，非 :523）。无独立 invalid-arg 错误码：多数函数返回 `MALFORMED_RESPONSE`，`send_token`（:510-513）与 `transfer_data`（:809-811）返回 `BUFFER_TOO_SMALL` |
| `stmin_ms` 实际保存 raw ISO-TP encoding，不一定是毫秒 | `gateway/src/transport/isotp_channel.h:8` | 字段名与真实语义不一致 | ✅ 成立。`isotp_channel.h:8-14` 注释自认 raw ISO-TP 编码（0xF1-0xF9 为 100µs）；默认值 2（profile.h:12）落在 ms 兼容区间，掩盖了不一致 |
| `socketcan_raw.c` 同时负责 CLI hex parsing 和 SocketCAN 生命周期 | `gateway/src/transport/socketcan_raw.c:18`、`:84` | 文档允许，但文件内职责已偏宽 | ✅ 成立。hex 解析（:18/:41/:60）与生命周期（:84）同文件；**额外发现**：解析函数与 `socketcan_raw_open` 均无生产调用（仅 `test_socketcan_args.c`），`open_filtered` 才是被 log_receiver 使用的接口 |
| `ota_worker.c` 同时负责 CLI、进程加固、文件安全、signer、reconnect、OTA orchestration 和结果输出 | `gateway/apps/ota_worker/ota_worker.c:132`、`:293` | 应用入口可组合，但当前文件偏重 | ✅ 成立。文件覆盖 CLI/加固/文件安全/signer/reconnect/编排/输出（:132-211/:213-266/:280-351/:369-385），但每项是薄 setup 层，核心逻辑已委托 `ota_executor`/`package_metadata`/`token_signer_client` |
| `ota_bridge.c` 同时负责 CLI、目录/endpoint 安全、ZeroMQ、子进程和 bundle staging | `gateway/adapters/swupdate/ota_bridge.c:265`、`:558` | 可能需要再拆成 process/runtime adapter | ✅ 成立。覆盖 CLI/目录与 endpoint 安全/ZeroMQ/subprocess/staging（:265-356/:410-448/:502-556/:622-711）；协议与 session 已委托 `gateway-bridge-support` 库 |

## 低置信度但建议保留的候选

| 嫌疑 | 证据 | 备注 | 复核结论 |
|---|---|---|---|
| `secure_zero()` 在 UDS、SecurityAccess、token signer 重复 | `gateway/src/uds/uds_client.c:40`；`gateway/src/security/security_access.c:7`；`gateway/adapters/token_signer/token_signer_client.c:31` | 可统一，也可能是有意避免跨层依赖 | ✅ 成立（行号修正：`uds_client.c:27` 非 :40）。三份 static 逐字节一致（volatile 指针清零循环）：uds_client.c:27-36、security_access.c:7-16、token_signer_client.c:31-40。**已处理**：2026-08-12 合并到 `src/util/util.c` 的 `secure_zero`，三处本地 static 全部删除 |
| `get_u32_be()` 在 UDS 和 OTA executor 重复 | 原始证据：`gateway/src/uds/uds_client.c:34`、`gateway/src/ota/ota_executor.c:59` | **已处理**：已抽到 `gateway/src/codec/byte_order.*`，保留本条用于防止回归 | ✅ 已处理确认。`src/codec/byte_order.h/.c` 存在；`uds_client.c:6` 与 `ota_executor.c:6` 均 include 并改用 `byte_order_get_u32_be`/`put_u32_be`，无本地残留定义。防回归保留正确 |
| `rename_noreplace()` 在 bridge 主文件和 session 文件重复 | `gateway/adapters/swupdate/ota_bridge.c:52`；`gateway/adapters/swupdate/ota_bridge_session.c:16` | 几乎是同一 helper | ✅ 成立。ota_bridge.c:52-66 与 ota_bridge_session.c:16-30 逐字节一致（含 `SYS_renameat2` + `ENOSYS` 回退），均为 static，抽取需公共头 |
| `write_exact()` 在 inner bundle 和 bridge session 重复 | `gateway/adapters/swupdate/inner_bundle.c:57`；`gateway/adapters/swupdate/ota_bridge_session.c:32` | 可抽成 adapter IO helper | ⚠️ 成立但需修正：算法一致（EINTR 重试 write 循环），但签名不同（`const void *` vs `const uint8_t *`）且失败码不同（-702 vs -806）；调用方依赖错误码区分路径，抽取时需保留映射 |
| `package_image_sha256()` 只是 `sha256_compute()` 的薄包装 | `gateway/src/package/package_manifest.c` | 当前没有外部调用点 | ✅ 已删除；镜像摘要直接在 `package_validate_image_buffer()` 中计算 |
| `McubootImageResult_t.security_counter` 目前主要被保存和测试读取，OTA 不消费 | 原 `gateway/src/package/mcuboot_image.*` | 确认是否为未来 policy 输入 | ✅ 已删除。Gateway 不再读取 MCUboot security counter；反回滚策略由 ECU bootloader 执行。 |
| `METADATA_ERR_IMAGE` 定义后未见使用 | `gateway/src/package/package_metadata.h:10` | 可能是遗留错误码 | ✅ 成立（行号修正：`package_metadata.h:17` 非 :10，:10 是 `METADATA_ERR_INVALID_ARG`）。全仓唯一孤儿错误码（-1008），兄弟错误码均有 raise 点；疑似重构遗留 |
| `swupdate_hawkbit_report_terminal()` 当前没有生产调用点，只被测试使用 | `gateway/adapters/swupdate/hawkbit.h:17`；`gateway/CMakeLists.txt:72` | 需要确认是预留 adapter 还是孤立模块 | ✅ 成立（CMake 行号修正：:72 是 `_GNU_SOURCE` 定义，与 hawkbit 无关；库定义 :74-80，测试专用链接 :204-211）。仅 `test_hawkbit.c`（11 处）调用，无生产 target 链接 |
| `BUILD_TARGET_BRIDGE_TEST_FIXTURES` 只声明，没有后续构建逻辑 | `gateway/CMakeLists.txt:114` | 疑似死配置 | ✅ 成立（行号修正：`CMakeLists.txt:118-119` 非 :114）。全仓仅 `option()` 声明与本文档提及，无 `if()` 分支，确为死配置 |
| `gateway-core` 虽然目录分层，但静态库把 package、OTA、UDS、Security、Linux ISO-TP 全部编在一起 | `gateway/CMakeLists.txt:22` | 源码边界尚可，构建边界偏粗 | ✅ 成立（行号修正：:23 非 :22）。gateway-core 编 12 个源、覆盖 6/7 个 `src/` 子目录，目录分层与库边界不对应；仅 log 独立成库（:56-61） |
| `gateway-bridge-support` 重新编译一份 `sha256.c` | `gateway/CMakeLists.txt:61` | 不是同一 binary 内重复，但存在 target 级重复 | ✅ 成立（行号修正：:66 非 :61）。`src/package/sha256.c` 同一翻译单元编译进 gateway-core（:32）与 gateway-bridge-support（:66）两个静态库；disk 上只有一个文件 |
| `OTA_BRIDGE_H` / `OTA_BRIDGE_SESSION_H` 在 `.c` 中再次定义 | `gateway/adapters/swupdate/ota_bridge.c:20`；`gateway/adapters/swupdate/ota_bridge_session.c:12` | 看起来像遗留的错误 include-guard 复制 | ⚠️ 需修正表述：ota_bridge.c:20-22 是**死宏**（树中无 `ota_bridge.h`，`OTA_BRIDGE_H` 无任何引用）；ota_bridge_session.c:12-14 因 `ota_bridge_session.h` 已定义该宏而为**无害 no-op**（`#ifndef` 为假，不构成重定义）。均为遗留，可删除 |
| `default_sleep_ms()` 对所有 `nanosleep()` 错误无限重试 | `gateway/src/ota/ota_executor.c:32` | 应只处理 EINTR，其他错误应退出 | ✅ 成立（行号修正：:33-43 非 :32）。while 循环不区分 EINTR（重试正确）与 EINVAL/EFAULT（会死循环）；仅 `config->sleep_ms==NULL` 时使用，且 delay 由 executor 自构造，实际风险低。**已处理**：2026-08-12 `default_sleep_ms` 迁至 `ota_snapshot.c`，仅 EINTR 重试、其他错误返回 -1；`OtaSleepMsFn` 返回类型改 `int`，worker 注入实现同步 |
| resume plan 的 checkpoint 乘法存在极端 uint32 溢出可能 | `gateway/src/ota/resume_transfer.c` | 当前 profile 尺寸下概率很低 | ✅ 已消除：计划结构、checkpoint 推导和 block count 已删除；当前 helper 只验证对齐并执行 `image_size - resume_offset` |
| download 的 `transferred_size + data_len` 存在极端溢出可能 | `gateway/src/ota/download.c:52` | 当前 image 上限下通常不会触发 | ❌ 降级为非问题（当前调用图不可达）。:52 的 uint32 求和理论上可回绕，但 chunk 被 `min(256, image_size-offset)` 钳制（resume_transfer.c:114-116），sum 恒 ≤ expected_size；防御性检查成立，可保留或加注释 |
| `socketcan_raw_open()` 隐藏注入默认 filters，而 `open_filtered()` 才是通用接口 | `gateway/src/transport/socketcan_raw.c:130` | `open` 的语义可能过于隐含 | ✅ 成立。`socketcan_raw_open`（:130-141）硬编码 `{CAN_RSP_ID}`/`{CAN_FUNC_ID}` filters 委托 `open_filtered`；`open` 本身零调用。若保留，建议注明默认 filter 语义或删除 |
| `socketcan_raw_recv()` 将 timeout、poll error、read error 都压成 `-1` | `gateway/src/transport/socketcan_raw.c:161` | 调用方难以区分异常类型 | ✅ 成立。:161-177 将 poll 超时/POLLERR/read 失败一律返回 -1；唯一调用方（`log_receiver_main.c:47`）只能区分 0 与 -1 |
| observer API 当前基本只被 UDS 测试使用 | `gateway/src/uds/uds_client.h:97` | 确认是否是正式 telemetry 扩展，还是测试遗留 | ✅ 成立。生产代码（src/ota、apps、adapters）无任何 observer 调用；仅 `test_uds_client.c`（:59/:326/:488/:622/:658/:693-701）使用。需确认 telemetry 计划或清理 |

## 建议 review 顺序

1. `ota_executor` 的结果状态模型与 `ota_bridge_session` 的完成状态（含"传输完成" vs "任务成功"语义、abort no-op）。
2. `package_manifest` / `package_metadata` 的重复 API 和重复 hash（一次计算、两处比较）。
3. UDS raw/typed API 以及两套超时政策（先裁定 `uds-ecu-alignment.md` 文档矛盾）。
4. worker、bridge、bundle 之间的重复契约常量（目录名与成员文件名）。
5. 最后再处理 helper、错误码、CMake 和低概率边界项。

## 使用说明

- ✅ / ⚠️ / ❌ 标记为本轮复核结论（2026-08-12）。⚠️ 条目请按"复核结论"列修正后的表述重新评估。
- 复核列中的行号已修正，后续定位以复核列为准；原证据列保留审查快照原文。
- 先确认条目是否成立，再决定是删除 API、改名、抽公共模块，还是拆分职责。
- "重复"不自动意味着必须合并；如果合并会引入跨层依赖，应记录保留理由（如 `secure_zero`）。
- 已处理条目（`get_u32_be`）继续保留，作为回归检查清单，避免重复实现重新进入业务模块。
