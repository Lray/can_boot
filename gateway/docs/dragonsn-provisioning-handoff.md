# DragonSN 烧录交接文件 (Handoff)

> **状态:✅ 已完成(2026-08-16)。** ROTPK + SSK(出厂预置)+ ecc_key 全部就位,
> token-signer-daemon 通过 OP-TEE 签名全链路验证(签发 + 验签 SUCCESS)。

## 最终结果

- 安全固件 + ROTPK:`sunxi_secure: secure`、`sunxi_rotpk: 1`
- SSK:出厂预置有效(efuse "已烧"状态即出厂值,keybox 加解密正常)
- ecc_key:keybox 加密存储,optee 正确解密
- ecdsa TA 公钥 `9a574e42...51a80` 与 daemon 常量匹配
- E2E:token 187B,COSE Sign1 验签 SUCCESS

## 关键经验(后续板子)

1. **固件必须是安全固件**(pack_secure)——非安全固件下 uboot 明文写 keybox、optee 按加密读 → 乱码
2. **烧录顺序**:安全固件镜像 → ROTPK → (SSK 若出厂已烧跳过)→ ecc_key
3. **数据格式**:rotpk 用工具专用 rotpk 类型;ecc_key/ssk 用"二进制文件"类型选文件(不能"固定值"填 hex 文本——uboot 按长度/格式校验)
4. **rotpk 值 = createkeys 的 rotpk.bin 内容**(非标准 SHA256,专有算法)

## 0. 重要结论(2026-08-16 实测)

**非安全固件下 keybox 无法工作**(uboot 明文写、optee 按加密读 → 乱码)。
**必须使用安全固件**(secure boot)。安全固件已生成:

- 镜像:`E:\T527\02-Images\02-Images\myd_lt527_emmc_buildroot\myd_lt527_emmc_buildroot_secure_v0.img`
  (SHA256 `C47980F6EE9F99DFFBB42766FAC33F13FFC87952D5C05DD14A3A0C0112F22A7D`)
- 签名密钥(妥善保管,配套使用,丢失即失去设备控制权):
  `E:\T527\secure-keys-20260816\`(RootKey_Level_0 / TrustedFirmwareContentCertPK / NonTrustedFirmwareContentCertPK + rotpk.bin)
- ROTPK:`rotpk.bin`(32B,hex `ccb90aedde7c4aad53bb8c322e46c23d6c28d2a91575ab335b178082cc5ae119`)— 烧录到 efuse,一次性,与安全固件配套

⚠️ 安全固件烧录后,普通固件无法再启动(启动链签名校验);rotpk 不可逆。
烧录安全固件前务必确认密钥已备份。

## 1. 任务目标与验收标准

1. **烧录 ROTPK**(efuse,一次性,安全固件配套)
2. **烧录 SSK**(efuse,一次性)— keybox 数据加密的前提
3. **烧录 `ecc_key`**(96 字节 d‖x‖y)→ 安全 key / keybox
4. **板端验证通过**:

```sh
# 板端(adb root 可用)运行,预期:启动无报错、idle 3 秒后退出码 0
/run/media/mmcblk0p6/ecu-ota/bin/token-signer-daemon \
  --uid 200 --gid 201 --socket-gid 201 --client-uid 0 --client-gid 0 \
  --idle-timeout 3 --endpoint /run/ecu-token-signer/v1.sock
```

- 成功 = daemon 通过 TA 会话(公钥校验)进入服务态后 idle 退出
- 失败 = 打印 `token-signer: secure startup failed`(keybox 缺 key 或公钥不匹配)

## 2. 环境与已验证事实

| 项 | 状态 | 证据 |
| --- | --- | --- |
| 板子 | adb 在线,serial `00675779d0c446e21d4`,root | `adb devices` |
| 新镜像已烧录 | OP-TEE 3.7 运行 | 板端 `/dev/tee0` `/dev/teepriv0` 存在;dmesg `optee: revision 3.7 (3b48fe6e)` |
| `burn_key=1` | 已生效 | u-boot.fex 内嵌 dtb `target` 节点 `burn_key = 1`(已解析确认) |
| `keybox_list` | 含 `ecc_key` | `keybox_list=hdcpkey,widevine,ecc_key`(env.fex 已确认) |
| tee-supplicant | 已部署运行 | `/usr/sbin/tee-supplicant`(PID 运行中) |
| libteec | 已部署 | `/usr/lib/libteec.so.1` → `libteec.so.1.0.0` |
| TA | 已部署 | `/lib/optee_armtz/724b12aa-6e74-4779-bf3a-1580a076fed3.ta` |
| daemon | 已部署(新公钥版) | `/run/media/mmcblk0p6/ecu-ota/bin/token-signer-daemon`(SHA256 `bdee4b0a...`) |
| keybox 当前状态 | **空(未烧录)** | 官方 keybox-read TA 读 `ecc_key`/`hdcpkey` 均返回 `0xffffffff` |

## 3. 文件位置

```
E:\download\烧号工具dragonsnv2.5.1\           ← DragonSN v2.5.1 工具包
├── DragonSN.exe                               ← 烧录主程序
├── DragonKeyConfig.exe                        ← 密钥配置工具(先配它)
├── DragonSN.pdf                               ← 官方使用说明(14 页)
├── DragonKey V2 配置工具使用说明.pdf          ← 官方配置说明(21 页)
├── ecc-key-material\                          ← 本次任务密钥物料
│   ├── ecc_key.bin                            ← 96B 烧录用文件(d‖x‖y,大端)
│   ├── can-ota-p256-new.pem                   ← 私钥!离线保管,勿提交/勿上设备
│   ├── ssk.bin                                ← 32B 随机 SSK(efuse 烧录用)
│   └── ssk_na                                 ← 官方 SSK 烧录工具(aarch64,板端执行)
└── Record\                                    ← 烧录记录(可看历史)
```

补充说明:
- `ecc_key.bin` = 96 字节原始二进制:`d(32) ‖ x(32) ‖ y(32)`,与 daemon 编译期
  公钥常量 `expected_public_x963` 匹配(常量在
  `E:\T527\can_boot\gateway\apps\token-signer-daemon\token_signer_daemon_main.c`)
- 私钥 `.pem` 只是离线备份,烧录只用 `ecc_key.bin`

## 4. 官方文档要点(已提取,PDF 内截图可对照查看)

### DragonSN.pdf 要点
- 烧号前提:固件 `[target] burn_key = 1`(已满足)
- 依赖全志 USB 驱动(装 APST 时自动装;工具包自带驱动需确认)
- **设备必须完全断电 → USB 连接 → 开机**(严禁以刷固件模式开机)
- 烧写成功后烧号口关闭(防重复烧);需重复烧需 PhoenixWipe 擦除
- 标识串来源:扫描枪/txt/excel/csv/固定值/递增递减/目录/数据库
- "自动烧录"勾选后识别设备即烧

### DragonKey V2 配置工具说明要点
- **全局配置**:安全 Key vs 私有 Key 二选一(见下)
- "设置标志位":0=烧完不设标志(可重复烧,开发用);1=烧完关闭烧号口(产线)
- Key 名称规则:仅 A~Z a~z 0~9 下划线,不可重复,不可为空
- Key 类型含"二进制文件"类型(正好用于 96B 的 ecc_key)

## 5. 配置参数(必须严格一致)

| 参数 | 值 | 说明 |
| --- | --- | --- |
| 全局 key 类别 | **安全 Key(secure key)** | 不能选私有 key——私有 key 进 private 分区,安全 key 才进 secure storage/keybox |
| 标志位(开发期) | 0 | 允许重复烧录;产线改为 1 |
| Key 1 名称 | `ecc_key` | 必须与 env 的 `keybox_list` 完全一致 |
| Key 1 类型 | 二进制文件 | 源文件 `ecc-key-material\ecc_key.bin` |
| Key 2 名称 | SSK(具体名按工具 efuse 列表) | efuse 类型;efuse map 只允许特定名称,以 DragonKeyConfig 中 efuse 类别下拉列表为准 |
| 烧录顺序 | **先 SSK,后 ecc_key** | keybox 数据由 secure OS 用 SSK 加密,SSK 必须先存在 |

## 6. GUI 操作步骤(按 PDF 控件描述,请结合 PDF 截图确认)

### 6.1 DragonKeyConfig.exe 配置
1. 打开 `DragonKeyConfig.exe`(自动读取现有配置;当前是 SN/MAC 示例)
2. 删除或保留现有 SN/MAC 项均可(与本次无关,但建议删除避免误烧)
3. 打开"全局配置":
   - 类别选 **安全 Key**
   - "设置标志位"= 0(开发)
   - 数据库相关全部留空
   - 保存
4. 添加 Key:
   - 类型 = **二进制文件**
   - 显示名称(随意,如 `ecc-key`)
   - **Key 名称 = `ecc_key`**(必填且精确,匹配 keybox_list)
   - 选择文件 = `ecc-key-material\ecc_key.bin`(96 字节)
   - 保存
5. 关闭配置工具,配置即生效(DragonSN 读取同一配置)

> 配置工具没有 efuse 类型;SSK 不用在这里配,见 6.2。

### 6.2 SSK 烧录(必须走 DragonSN GUI)

> ⚠️ **实测结论(2026-08-15)**:SDK `optee-ssk` demo 的 `ssk_na`(PTA 直烧
> efuse)在本板**不可用**——执行返回 `burn_efuse:finish with ffffffff`;
> 且 optee 的 efuse read syscall 对 chipid/ssk 均返回 0 字节。uboot 源码
> (`board/sunxi/key_burn.c`)证实正规烧录通道是 **uboot 启动阶段 USB 会话**
> (`do_burn_from_boot`),即必须由 DragonSN.exe 通过 USB 完成。ssk_na 已
> 不再使用(保留在物料目录仅供参考)。

SSK 通过 DragonSN 配置并烧录(对应安全指南 7.1「DragonSN 烧录安全 efuse
配置」图 7-1)。配置时注意:

- DragonKeyConfig 类型列表无显式 "efuse" 项,efuse key 很可能以
  **"固定"类型 + efuse map 规定的 key 名称** 配置,或工具界面有独立 efuse
  配置入口——**以工具实际界面为准,请截图确认**(这正是 GUI 环节)。
- key 名称:`ssk`(efuse map 名称,optee demo 中硬编码为 `"ssk"`)
- key 值:`ssk.bin` 的内容(32 字节),输入格式(hex/ASCII)以工具界面为准
- **先烧 SSK,再烧 ecc_key**(ecc_key 的 keybox 数据由 SSK 加密)

⚠️ efuse 不可擦除。`ssk.bin` 与 `ecc_key.bin` 必须**配套保管**:keybox 数据
由 secure OS 用该 SSK 加密,烧录后不可更换 SSK,否则已烧的 keybox 数据无法解密。
`ssk.bin` 十六进制内容(可直接复制到工具):

```text
f860a5d689e21fb54ff0811cbc92a192612599231d1cd0e5accfb5426ccfb38e
```

### 6.3 DragonSN.exe 烧录(SSK 与 ecc_key 均在此完成)
1. 板子**完全断电**,USB 连接 PC(注意:板子上电后 adb 会断,属正常)
2. 打开 `DragonSN.exe`,确认主界面显示 `ecc_key` 与 SSK 配置项
3. 给板子上电(正常开机,不是刷机模式),工具应识别设备
4. 先烧 SSK,再烧 ecc_key;点"烧录"→ 观察界面输出与 Record\ 目录新增 XML
5. 烧完 adb 重新上线,执行第 1 节验证命令

## 7. 排障

| 现象 | 处理 |
| --- | --- |
| 工具识别不到设备 | 检查 USB 驱动;确认断电插线再开机;确认固件 burn_key=1(已确认) |
| 烧录失败/超时 | 看 Record\*.xml 与工具输出;确认没用刷固件模式开机 |
| daemon 仍报 secure startup failed | 板端 `ls /data/tee` 看 secure storage 是否有新文件;用 keybox-read 测试 TA 复测(见第 8 节);核对 `ecc_key.bin` 与 daemon 公钥常量是否一致(重新生成 bin 与常量会失配) |
| 需要重复烧录 | 全局配置标志位=0 或 PhoenixWipe 擦除 |

## 8. 板端诊断命令(adb)

```sh
# keybox 状态复测(编译/部署见下):官方 keybox-read TA 读 ecc_key
# 返回 0xffffffff = 未烧;返回 0 = 已烧(输出 1024 字节缓冲)
adb -s 00675779d0c446e21d4 shell "LD_LIBRARY_PATH=/usr/lib /data/local/tmp/teec-selftest"   # 若该文件还在

# 简单观察 secure storage
adb -s 00675779d0c446e21d4 shell "ls -la /data/tee"
```

若需要重建 keybox 测试程序:
- 源码:历史会话中 `teec_selftest.c`(已从仓库删除),用 export-ca 头+libteec.a 交叉编译
- 相关 SDK 参考:`E:\T527\04-Sources\MYD-LT527\platform\allwinner\security\optee\demo\optee-keybox-read\`
  (官方 keybox 读取 demo,含 TA 与 CA)

## 9. 安全注意事项

1. `can-ota-p256-new.pem` 是签名私钥:**严禁提交到任何仓库、严禁推送到设备**
2. 烧录完成后建议把全局标志位改回 1(关闭烧号口)再产线使用
3. 本镜像为非安全固件(`pack` 非 `pack_secure`),ROTPK 未烧;如需 secure boot 另走 `pack_secure` 流程
4. 密钥变更后,daemon 的 `expected_public_x963` 常量必须同步更新并重新编译,否则 daemon 拒绝启动

## 10. 相关文档

- `E:\T527\can_boot\gateway\docs\optee-token-signer.md` — 架构与板端部署总文档
- `E:\T527\04-Sources\MYD-LT527\docs\Software 软件类文档\SDK模块开发指南\SECURE\Linux_安全_开发指南.pdf` — 官方安全指南(7 章密钥存储、4.4 ROTPK 烧写、5 章 OPTEE)
- 工具包内两份 PDF(截图齐全,建议逐图对照)
