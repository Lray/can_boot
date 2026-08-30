# 升级包格式审查:各文件必要性分析

> **已过时**：本文是 2026-08 对 v2 三成员格式的评审记录，结论已被 v2 → v3 简化取代。
> 现行规范见 `ota-package-format.md`：inner bundle 为 `package-metadata-v3.json` + `ecu-image.bin` 两成员，
> manifest.bin 删除（字段全部可从 MCUboot 镜像推导），metadata 仅保留 release/family 身份。

## 一、包格式全景(三层结构)

```text
┌─ SWU 包 (134128B, hawkBit 下发, swupdate 官方格式) ─────────────┐
│  sw-description        363B   ← 描述 images 清单(remote handler 目标) │
│  sw-description.sig    850B   ← RSA-PSS 签名(swupdate 强校验)        │
│  gateway-input-v1.cpio 132592B ← inner bundle(newc cpio)            │
└────────────────────────────────────────────────────────────────────┘
                    │ bundle_extract 解包
                    ▼
┌─ inner bundle (cpio 3 成员) ───────────────────────────────────────┐
│  package-metadata-v2.json   471B   ← 发布元数据(JSON)               │
│  ecu-manifest.bin            48B   ← 固件清单(二进制定长)            │
│  ecu-image.bin           131072B   ← MCU 固件镜像(升级本体)          │
└────────────────────────────────────────────────────────────────────┘
```

## 二、逐文件必要性分析

### 1. SWU 外层(sw-description + sig + cpio)——全部必须,不可省

| 文件 | 是否可省 | 依据 |
|---|---|---|
| sw-description | **不可省** | swupdate 官方格式强制:无 sw-description 则 swupdate 无法解析包内容。其中 `type = "remote"; data = "ecu-v1"` 决定了**走 Remote Handler (ZMQ) 通道**。 |
| sw-description.sig | **不可省(安全场景)** | swupdate 用信任公钥校验签名,无签名则拒绝安装(签名是包来源可信的唯一依据)。 |
| gateway-input-v1.cpio | 不可省(当前设计) | 它是"内层业务包"的容器;若去掉则 swupdate 会把 sw-description/sig 直接交给 remote handler,业务文件无法独立于 swupdate 格式演进。 |

### 2. inner bundle 三个成员——逐一审查

#### a) `ecu-image.bin`(131072B)——**必须,升级本体**

- worker 通过 UDS 传输到 MCU 的唯一数据来源。
- 无它则无升级。

#### b) `ecu-manifest.bin`(48B)——**必须,且是升级决策的"锚"**

worker 在 `ota_executor` 中**直接依赖 manifest 字段**做运行时决策:

| manifest 字段 | worker 用途(证据:ota_executor.c) |
|---|---|
| `image_size` | pending 校验(`ota_executor.c:90-92`)、下载尺寸约束 |
| `image_version` | 升级成功判定：完整 MCUboot 四段版本逐字段比较（`ota_executor.c`） |
| `image_sha256` | `package_validate_image_buffer` 镜像完整性校验；作为 UDS `payload_id` |

**manifest 不是"冗余壳",它承担了 worker 与 ECU 交互的运行时契约**:
- 续传时 worker 将 `manifest.image_sha256` 原值作为 `payload_id` 发送给 ECU；ECU 只在
  `payload_id + image_size + target_slot + 有效 checkpoint` 全部匹配时续传;
- 升级后 worker 读 ECU 的 `DID_APP_VERSION`,必须与 `manifest.image_version` 一致才算成功。
- **这两个字段必须随包下发,且独立于 ECU 上的任何持久化状态**。

#### c) `package-metadata-v2.json`(471B)——**必须,但字段有精简空间**

| 字段 | 必要性 | 说明 |
|---|---|---|
| `schema` | 必须 | 格式版本门禁 |
| `release_id` | 必须 | 发布批次身份(与部署期望比对) |
| `package_id` | 必须 | 由其余 9 个字段**计算出的哈希自证**(`compute_package_id`),防篡改 |
| `ecu_family` | 必须 | 目标 ECU 家族门禁 |
| `target_slot_policy` | 必须 | 目标槽策略 |
| `manifest_sha256` | 必须 | **metadata → manifest 的绑定链**(校验 manifest 未被替换) |
| `ecu_image_key_id/version` | 必须 | 镜像签名密钥选择(供 MCU 侧验签) |
| `token_key_id/version` | 必须 | SecurityAccess token 密钥选择 |

**关键**:`package_id` 是"元数据自证哈希",`manifest_sha256` 是"元数据→manifest 绑定",
manifest 内含 image 的 SHA256，形成**三层哈希链**:
`metadata → manifest → image`。这是防"整体替换包内文件"的安全设计,不可去掉任何一环。

## 三、回答核心问题:"是否都能必须?"

**结论:当前 6 个文件(3 SWU 外层 + 3 内层)全部是必须的**,理由:

1. **SWU 外层**:swupdate 官方格式强约束,无法绕过(权威工具)。
2. **inner bundle 三成员**各有不可替代职责:
   - image:升级本体
   - manifest:**运行时升级决策锚**(续传身份、版本判定)——worker 直接读它做 UDS 交互,不是装饰
   - metadata:发布身份 + 三层哈希链起点(防替换)
3. 无冗余文件:没有"预留兼容壳"或未消费字段
   (`make_sample_package.py` 生成的 `package.info/source/sha256` 是构建期副产品,不进包)。

## 四、"只有一个镜像包能否完成升级?"

**不能,分两层回答:**

### 1. 若"一个镜像" = 只发 `ecu-image.bin`(无 metadata/manifest)

**不能**,因为 worker 升级需要:
  - **升级前**:`manifest.image_size`(下载尺寸)、`manifest.image_sha256`(完整 image 的 payload_id)——
  这些**在升级开始前就必须知道**,无法从 image 本身获得
  (image 是二进制,先解析 image 再决定是否升级 = 先下载了才知道该不该下载,逻辑倒置)。
  - **升级中**:续传在同一 update-state checkpoint 内比对完整 image 的 `payload_id`、
    `image_size`、目标槽位与已提交 offset；
- **升级后**:需要 `image_version` 判定成功。
- **安全**:无法校验 image 的哈希/签名密钥来源(无 metadata 绑定链)。

### 2. 若"一个镜像" = 把 metadata+manifest 字段并入 sw-description 或单一 JSON

**理论可行,但不推荐,且不省成本**:

| 方案 | 问题 |
|---|---|
| 字段塞进 sw-description | sw-description 是 swupdate 格式,加自定义字段 swupdate 会**解析失败或忽略**;且 description 不随 inner bundle 到达 worker(remote handler 只收到 cpio) |
| metadata+manifest+image 合成单文件 | 需在 worker 里重新实现"二进制里嵌 JSON+哈希"的自定义解析——**等于再造一个 manifest,违背"不造轮子"**;且大镜像(256MiB 级)单文件流式校验困难 |
| 只用 metadata(去掉 manifest) | worker 的续传/版本判定/硬件校验字段(全部在 manifest)就必须搬进 metadata JSON——**JSON 里塞二进制敏感字段 + 每次升级都重复解析,反而更复杂**,且失去"定长二进制可快速 parse"的优势 |

**关键事实**:manifest 只有 48B,metadata 只有 471B,两者合计 < 0.5KB,
占整个 SWU(134144B)的 0.4%。**合并它们省下的空间可忽略,却损失了**:
- 定长二进制快速校验(manifest 无需 JSON 解析);
- worker 运行时直接读结构体字段;
- 三层哈希链的安全绑定。

## 五、结论

1. **当前包格式无冗余文件,均为必要**;唯一的"看起来多余"是 metadata 与 manifest
   字段的部分重叠(如 image_size/sha256 在 manifest,manifest_sha256 在 metadata),
   但这正是**三层哈希链**的设计,不是重复。
2. **不能仅用一个镜像包完成升级**;最小可行包 = `sw-description + sig + cpio(image+manifest+metadata)`,
   即当前格式本身。
3. 若未来要精简,唯一合理的演进是:把 `metadata` 的 `target_slot_policy`/密钥字段
   与 manifest 合并(纯配置字段),但 `release_id/package_id/manifest_sha256` 绑定链必须保留。
   当前规模下(0.5KB 开销)不建议为此改动。
