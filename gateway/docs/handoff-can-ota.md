# T527 CAN OTA 全链路交接文档

> 交接时间:2026-08-16
> 目的:让新 session 无缝继承全部工作状态,继续完成 gateway→MCU 升级链路测试与提交。

## 1. 项目与目标

T527 网关 + STM32U5A9 ECU 的 CAN OTA 升级系统。核心安全设计:
**网关 token-signer-daemon 通过 OP-TEE 安全世界持有 P-256 私钥,私钥字节永不出安全世界。**

目标状态:① 安全烧录完成 ② gateway→MCU OTA 升级链路验证通过 ③ 代码提交。

## 2. 当前进度一览

| 项 | 状态 |
| --- | --- |
| OP-TEE 安全链路(daemon 签名 + 验签) | ✅ 完成并验证 |
| 板端安全固件 + ROTPK + SSK + keybox ecc_key | ✅ 完成 |
| gateway→MCU OTA 升级链路 | ⚠️ **进行中,卡在手动烧录步骤** |
| 代码提交 | ⏳ 未提交(有大量改动) |

## 3. 待办(新 session 第一步)

### 3.1 等待用户手动烧录(已交付文件)

用户将**全片擦除**并手动烧录:
1. bootloader
2. **0x081F6000 ← `C:\Users\86151\Desktop\bootmcu\ymodem-slot0-v100\boot-security-counter-15-record.bin`**(安全计数器=15,必须!)
3. **slot0 ← `C:\Users\86151\Desktop\bootmcu\ymodem-slot0-v100\image.bin`**(固件 1.0.0,confirmed,新公钥)

烧录完成后验证:
```sh
# 板端(awlink0 恢复:先配置 CAN 并拉起，再执行 ip link set awlink0 mtu 16)
python3 /data/local/tmp/read_mcu_version.py awlink0
# 预期:APP_VERSION=0x01000000(1.0.0), ACTIVE_SLOT=0
```

### 3.2 恢复 OTA 升级测试(关键修正)

**历史失败根因(已查明):**
- 之前升级包用了 `--target-slot 0` 链接的镜像,但升级目标是 slot1(inactive)
- **MCUboot 报错 `Invalid APP vectors: APP=0x08030200`** — slot1 固件必须链接到 slot1 payload base(0x08030200)
- **正确打包:构建 Debug-slot1 preset + `make_ecu_mcuboot_bundle.py --target-slot 1`**

新升级包要求:
- 版本 > 1.0.0(如 1.0.1)
- SEC_CNT >= 15(建议 16)
- token 签名:**恢复用正式 OP-TEE daemon(新密钥)**——MCU 1.0.0 固件内置新公钥 9a574e42...,与 keybox 匹配!不再需要临时 signer

```sh
# 构建 slot1 链接固件
cd E:\T527\can_boot\can
cmake --preset Debug-slot1
cmake --build --preset Debug-slot1
python scripts\make_ecu_mcuboot_bundle.py --target-slot 1 --version 1.0.1 \
  --security-counter 16 --payload build\Debug-slot1\Can.bin \
  --out-dir build\ota-slot1-v101 --key E:\Simple_ST\Boot\keys\root-ec-p256.pem \
  --imgtool E:\Python314\Scripts\imgtool.exe

# image.bin 为唯一包成员,直接作为 SWU remote artifact 发布到 package-input-v1。
# 启动 orchestrator 时还须传入该已发布 image 的 size/SHA-256，bridge 在启动
# worker 前校验它；SWUpdate 签名与 MCUboot 验签继续分别保护外层 SWU 和 ECU。
```

### 3.3 板端运行时部署与测试流程

```sh
# 部署(全片擦除后 rootfs 可能还在,若重烧镜像需重装):
# tee-supplicant/libteec/TA/daemon(见 gateway/docs/secure-boot-provisioning-record.md)

# 恢复临时 signer(旧密钥,仅当 MCU 还是 1.0.0 之前需要——新固件后不需要)
sh /data/local/tmp/start_temp_signer.sh   # 临时 python signer(旧密钥)

# 恢复正式 daemon(新密钥)
killall python3; /run/media/mmcblk0p6/ecu-ota/bin/token-signer-daemon ... &

# 升级测试
sh /data/local/tmp/run_worker3.sh  # 需更新 release/package id
# 注意:MCU reset 后约 40-60 秒恢复,worker post-reset 窗口可能不够
```

## 4. 环境与关键文件

### 4.1 板端(adb 00675779d0c446e21d4)

| 文件 | 位置 | 状态 |
| --- | --- | --- |
| tee-supplicant | /usr/sbin/ | 部署(手动启动) |
| libteec.so.1 | /usr/lib/ | 部署 |
| ecdsa TA | /lib/optee_armtz/724b12aa-...ta | 部署(诊断版含错误区分) |
| token-signer-daemon | /run/media/mmcblk0p6/ecu-ota/bin/ | 部署(正式,新公钥) |
| gateway-ota-worker-v1 | /usr/bin/ | 部署 |
| 临时 signer | /data/local/tmp/temp_signer.py + can-ota-p256-private.pem | 临时(旧密钥) |
| 工具脚本 | /data/local/tmp/{read_mcu_version.py, ulog_capture.py, run_worker3.sh, start_temp_signer.sh, ecdsa_open_test, keybox_test, token_e2e_test, efuse_na} | 在 |

**注意:MCU 重启后 tee-supplicant/daemon 需要重新手动启动。awlink0 配置:**
```sh
ip link set awlink0 down; ip link set awlink0 type can bitrate 500000; ip link set awlink0 up; ip link set awlink0 mtu 16
```
⚠️ **MTU 必须是 16,604(CAN-FD)下 MCU 无响应!**

### 4.2 PC 端关键路径

| 内容 | 路径 |
| --- | --- |
| 签名密钥组(固件链) | E:\T527\secure-keys-20260816\(ROTPK=ccb90aed...) |
| 烧录文件(rotpk/ssk/ecc_key) | C:\Users\86151\Desktop\token_key\key\ |
| **ECU 私钥(新,唯一)** | C:\Users\86151\Desktop\token_key\key\can-ota-p256-private.pem |
| **ECU 私钥(旧,dfad3662...)** | C:\Users\86151\Desktop\key\can-ota-p256-private.pem(临时升级用) |
| 安全固件镜像 | E:\T527\02-Images\...\myd_lt527_emmc_buildroot_secure_v0.img |
| Boot 源码 | E:\Simple_ST\Boot(看门狗已恢复) |
| can 源码 | E:\T527\can_boot\can(喂狗线程已加) |
| gateway 源码 | E:\T527\can_boot\gateway |
| 待烧录文件 | C:\Users\86151\Desktop\bootmcu\ymodem-slot0-v100\(image.bin + counter bin) |

### 4.3 构建命令速查

```sh
# can 固件构建(Windows)
cd E:\T527\can_boot\can
cmake --preset Debug-slot0|Debug-slot1   # slot0=0x08010200, slot1=0x08030200
cmake --build --preset Debug-slot0

# 打包(Windows)
python scripts\make_ecu_mcuboot_bundle.py --target-slot N --version X.Y.Z \
  --security-counter N --payload ... --key E:\Simple_ST\Boot\keys\root-ec-p256.pem \
  --imgtool E:\Python314\Scripts\imgtool.exe [--provisioning]

# gateway 目标构建(WSL)
bash /mnt/e/T527/can_boot/gateway/scripts/build_target_wsl.sh build-target-tee

# TA 构建(WSL)
bash /mnt/e/T527/can_boot/gateway/scripts/build_ta_sdk.sh
```

## 5. 关键知识(踩坑记录)

1. **OP-TEE keybox 必须安全固件**:非安全固件下 uboot 明文写 keybox、optee 按加密读 → 乱码
2. **v3 包无 package_id**:descriptor 只有 schema/release_id/ecu_family;镜像摘要由 gateway 加载时计算,作为续传 payload_id(见 package_metadata.c)
3. **release_id 必须小写 UUID**(worker 校验 variant 位 '8','9','a','b')
4. **package-input-v1 目录权限**:dir 0700,文件 0400,owner root;job_dir basename == job_id
5. **MCU 固件 slot 链接**:升级到 slot1 必须用 Debug-slot1 构建(否则 vector 错误)
6. **安全计数器**:双区存储(0x081F6000/0x081F8000),全片擦除后必须先烧记录,否则 boot 拒绝所有镜像
7. **awlink0 MTU 必须 16**
8. **image_ok=3 现象**是 slot1 镜像 vector 错误启动失败导致的连锁表现(根因就是 slot 链接错误)
9. **worker post-reset 窗口**:MCU 重启后恢复约 40-60 秒,worker 快照可能超时(INDETERMINATE)——必要时考虑调整窗口或重试

## 6. 代码改动清单(待提交)

### can/ 项目
- `security/security_crypto_port.c`:公钥更新为 9a574e42...(新密钥)
- `watchdog/watchdog.{c,h}`:新增(仅喂狗)
- `Core/Src/main.c`:喂狗线程 + 立即喂狗
- `CMakeLists.txt`:加 watchdog 模块

### gateway/ 项目
- `optee/ecdsa-p256-sign/`:TA 源码(含诊断版错误区分,建议清理为正式版)
- `adapters/token_signer/token_signer_tee.{c,h}`:TEEC 集成(新)
- `adapters/token_signer/token_signer_daemon.c/h/main.c`:OP-TEE 化,移除 PEM
- `adapters/swupdate/orchestrator*.{c,h}`:移除 --signer-key
- `scripts/`:`build_ta_sdk.sh`(新)、`build_target_wsl.sh`(TEE)、deploy/HIL 脚本更新
- `docs/`:`optee-token-signer.md`、`secure-boot-provisioning-record.md`、`dragonsn-provisioning-handoff.md`、`hawkbit-gateway-hil.md`(WIP)
- 其余 WIP(未提交前已有):token_signer_codec.c、package_json.c、tests 等

### Boot 项目(E:\Simple_ST,独立仓库)
- `Core/Src/iwdg.c`:BOOT_IWDG_ENABLED=1 + 真实 IWDG_Feed

## 7. 待提交注意事项

- ECU 私钥、签名密钥组**严禁入库**
- 临时 signer 脚本/旧私钥用完删除
- 提交前:清理 TA 诊断代码、跑宿主测试、确认无 PEM 残留引用

## 8. 下一步行动(按顺序)

1. 等用户烧录完成 → 验证 MCU 1.0.0
2. 构建 slot1 链接的 1.0.1 升级包(SEG_CNT 16,新 metadata)
3. 恢复正式 OP-TEE daemon(替代临时 signer)
4. 跑 worker 升级 → 验证 MCU 到 1.0.1(slot1)
5. 确认全链路(下载/重启/确认/回滚路径)
6. 清理诊断代码 + 提交
