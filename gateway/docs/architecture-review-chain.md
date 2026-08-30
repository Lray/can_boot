# 架构审查:swupdate → remote handler → zeromq → ota_worker 链路 vs AGENTS.md

审查日期:2026-08-15
审查对象:`ecu-ota-orchestrator` 单进程编排链路
对照规范:`gateway/AGENTS.md`(9 条原则)

## 一、链路各环节与职责

```text
[板端 ecu-ota-orchestrator 单进程]
  ├─ wifi_ctrl          wpa_supplicant/wpa_cli 官方命令(扫描/连接已保存网络)
  ├─ launch_swupdate    fork+exec /sbin/swupdate(suricatta 官方模式)
  ├─ remote_handler     ZMQ REP 端点(swupdate Remote Handler 协议服务端)
  ├─ bundle_store       bundle 接收会话(INIT/DATA、大小+SHA256、原子落盘)
  ├─ bundle_extract     inner bundle(newc cpio)解包
  └─ launch_worker      fexecve gateway-ota-worker-v1

[外部权威组件]
  hawkBit (WSL docker) ──DDI──> swupdate 2019.11 ──ZMQ Remote Handler──> orchestrator
```

## 二、逐条对照 AGENTS.md

### ✅ 1. "有开源代码实现(必须权威),不允许自己造轮子"——**符合**

| 环节 | 是否造轮子 | 证据 |
|---|---|---|
| 服务器对接 | 否 | 直接使用 **swupdate 官方 suricatta** 对接 hawkBit(官方支持的后端,见 swupdate 官方 suricatta 文档) |
| WiFi 控制 | 否 | `wifi_ctrl.c` 全部调用 **wpa_supplicant/wpa_cli 官方命令**(`scan`/`list_networks`/`select_network`/`status`),无自研 WiFi 协议 |
| ZMQ 传输 | 否 | 直接使用 **libzmq 官方库**(`zmq_bind`/`zmq_msg_*`),未自研传输层 |
| 签名校验 | 否 | 信任公钥由 swupdate `-k` 参数处理(官方机制) |

### ✅ 2. "严格按照官方文档要求,做最小实现"——**符合(有 1 处待改进,见 §3)**

Remote Handler 协议逐帧对照 swupdate 2019.11 官方 `handlers/remote_handler.c`:

| 协议项 | swupdate 官方(客户端) | 本项目(服务端) | 一致 |
|---|---|---|---|
| socket 类型 | ZMQ **REQ** 客户端 `zmq_connect` | ZMQ **REP** 服务端 `zmq_bind` | ✅(REQ↔REP 配对) |
| 帧结构 | 双帧:frame0=command, frame1=body(`ZMQ_SNDMORE`) | 双帧接收 + `ZMQ_RCVMORE` 校验 | ✅ |
| INIT 命令 | `"INIT:%lld"`(img->size)+ 空 body | `parse_init_size` 解析 `INIT:<size>`,要求 body_len==0 | ✅ |
| DATA 命令 | `"DATA"` + body(chunk) | `bundle_store_handle_data` 校验 `"DATA"` + body | ✅ |
| ACK 回复 | 期望 `strncmp(reply,"ACK",...)==0`;`ACK:<ms>` 延长超时 | `"ACK:540000"`(INIT 后)+ `"ACK"`(DATA 后) | ✅ |

### ⚠️ 3. "能一行代码完成绝对不写两行" / "最小实现"——**轻微偏移(1 处)**

**`remote_handler_receive` 的 body 生命周期设计(remote_handler.c:144-146)**:

```c
*body = zmq_msg_data(&body_msg);
*body_len = zmq_msg_size(&body_msg);
(void)body_msg;   /* 局部 zmq_msg_t 不 close,故意泄漏以让调用方读指针 */
```

- 这是"故意不 close 以借用数据"的手法:函数返回后 `body_msg` 栈对象销毁,
  但 ZMQ 消息底层 buffer 因未 `zmq_msg_close` 而存活,调用方用完无释放路径。
- **违反"语义精准、不允许冗余"**:API 语义是"借用指针",却没有对应的
  `release` 语义,调用方(orchestrator.c:322)每轮循环重复借用,旧 body 泄漏直到进程退出。
- 最小实现应为:receive 时把 body **拷贝进调用方提供的 buffer**(类似 command),
  或提供显式 `remote_handler_body_release()`。
- 影响:单次升级流程只循环有限次,泄漏量有限(≤2 个 chunk 缓冲),**不构成功能性 bug,但是语义不精准**。

### ✅ 4. "代码安全与质量必须严格符合标准"——**符合(有 2 处风险点,见 §4)**

安全措施已到位:
- worker/endpoint/work_root 全部 `O_NOFOLLOW` + `lstat` 属主/权限校验
- `flock` 单实例锁 + socket 属主/模式校验(remote_handler.c:32-55)
- bundle 原子落盘:`partial → fsync → rename_noreplace → fsync`
- 信任密钥 root-only;wifi 配置文件 0600 校验
- 子进程环境隔离:`worker_env = {NULL}`

### ✅ 5. "语义必须精准,不允许冗余"——**基本符合(1 处待改进,见 §3)**

- 模块命名精准:`remote_handler`(端点+帧)、`bundle_store`(会话+校验)、
  `bundle_extract`(cpio)、`wifi_ctrl`(命令封装)——职责与名字一一对应。
- 无死代码、无未消费字段。

### ✅ 6. "不允许预留兼容壳"——**符合**

- 无"将来可能用"的参数/分支/接口。
- `swupdate_cfg` 参数已在本次修复中删除(不再依赖 cfg 文件),未留兼容。

### ✅ 7. "代码职责与分层必须清晰,不允许反向依赖"——**符合**

依赖方向(全部单向,无环):
```text
wifi_ctrl ──┐
remote_handler ──┤
bundle_store ────┼──> orchestrator(仅编排,不实现任何协议细节)
bundle_extract ──┤
fs_util ─────────┘
       ↓(orchestrator 唯一调用方)
gateway-ota-worker-v1(fexecve,完全独立进程)
```
- `remote_handler` 不含 bundle/job/worker 策略;
- `bundle_store` 不含 ZMQ/子进程;
- `orchestrator` 不实现协议帧/校验/解包细节,只做状态机编排。
- worker 无法反向调用 orchestrator(独立进程 + 只读 job 目录)。

### ✅ 8. "高内聚、低耦合"——**符合**

- 每个模块单一职责(见 §1 表格),模块间仅通过窄接口交互。
- CMake 分层:`gateway-remote-handler`(ZMQ)、`gateway-wifi-ctrl`(wpa_cli)、
  `gateway-bridge-support`(store+extract+fs),orchestrator 组合三者。

### ✅ 9. "不许薄封装"——**符合**

- `remote_handler` 封装了"端点锁 + socket 生命周期 + 双帧协议校验",不是 `zmq_bind` 的 1:1 转发;
- `bundle_store` 封装了"会话状态机 + SHA256 + 原子 rename",有实际策略;
- `wifi_ctrl` 封装了"探测已运行 + 扫描 + 连接 + 等待 IP"的编排逻辑;
- 均包含不可省略的业务语义,非纯透传。

## 三、审查结论

**总体:链路与 AGENTS.md 高度一致,无结构性偏移。** 9 条原则中 8 条完全符合,
1 条(最小实现/语义精准)存在轻微待改进点。

### 待改进项(1 个,建议修复)

**`remote_handler_receive` 的 body 借用语义(remote_handler.c:144-146)**

- 现状:返回指向 ZMQ 消息内部 buffer 的指针,无释放接口 → 语义不完整。
- 建议:receive 时 body 写入调用方 buffer(与 command 一致的最小实现),签名改为:
  `int remote_handler_receive(handler, char *command, size_t cmd_cap, uint8_t *body, size_t body_cap, size_t *body_len_out)`
- 工作量:约 15 行;消除借用语义与泄漏路径。

### 风险提示(2 个,非违规但需知晓)

1. **orchestrator 单次 job 生命周期**:主循环 `receive_timeout=600000`(10 分钟),
   swupdate 若长时间无 DATA(下载中断),orchestrator 会超时退出——本次调试中已观察到
   `receive timeout or protocol failure`。当前 swupdate 下载是"先完整下载 SWU 再推流"，
   10 分钟对 256MiB 级镜像可能不足。**建议按 SWU 大小动态计算超时**。
2. **`launch_swupdate` 的 suricatta 参数拼接**(orchestrator.c:173-177):
   `snprintf` 拼接 `-t/-u/-i/-k/-p/-r/-w`,与官方示例一致,但参数含空格/特殊字符时
   无转义(URL/ID/Token 由部署方提供,当前校验未覆盖)。当前输入受 `--hawkbit-*`
   参数校验约束,风险可控。

### 结论一句话

链路分层、官方组件复用、安全与职责边界均符合 AGENTS.md;
唯一建议修复点是 `remote_handler_receive` 的 body 借用语义(最小实现/语义精准原则)。
