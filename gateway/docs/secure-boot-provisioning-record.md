# T527 安全 OTA 签名链路 — 板端配置与烧录全记录

> 记录日期:2026-08-16
> 目标:网关 token-signer-daemon 通过 OP-TEE 安全世界持有 P-256 私钥,
> 网关只拿签名能力,私钥字节永不出安全世界。
> 状态:**已完成并端到端验证**(token 签发 + 验签 SUCCESS)。

## 1. 密钥体系(两套独立密钥,无对应关系)

| 密钥 | 用途 | 算法 | 位置/值 |
| --- | --- | --- | --- |
| **安全启动密钥组** | 固件签名/验证(secure boot 链) | RSA 2048 | `E:\T527\secure-keys-20260816\`(RootKey_Level_0 / TrustedFirmwareContentCertPK / NonTrustedFirmwareContentCertPK) |
| **ROTPK** | 根证书公钥哈希,烧入 efuse,验证安全固件 | 32B(专有哈希算法,非标准 SHA256) | `rotpk.bin` = `ccb90aedde7c4aad53bb8c322e46c23d6c28d2a91575ab335b178082cc5ae119` |
| **SSK** | keybox 数据加密密钥(OP-TEE 从 efuse 读取) | 32B | `ssk.bin` = `f860a5d689e21fb54ff0811cbc92a192612599231d1cd0e5accfb5426ccfb38e` |
| **ECU 签名密钥** | OTA token(COSE Sign1 ES256)签名 | P-256 | 私钥:keybox `ecc_key`(安全世界);公钥:`9a574e42...51a80`(daemon 编译期常量) |

**对应关系说明**:
- 安全启动密钥组(RSA)只用于固件签名链验证,与 ECU 的 P-256 签名密钥**完全独立,无任何关联**
- ECU P-256 公钥对应的是 keybox 中烧录的 `ecc_key.bin`(d‖x‖y,96B),与 daemon
  `token_signer_daemon_main.c` 中 `expected_public_x963` 常量一致
- 若需更换 ECU 签名密钥:重新生成 keypair → 更新 daemon 常量重编 → 重新烧录 `ecc_key`

## 2. 板端镜像配置改动(SDK,构建树 /home/lirui/work/t527/MYD-LT527,已同步 /mnt/e)

| 文件 | 改动 | 作用 |
| --- | --- | --- |
| `device/config/chips/t527/configs/default/boot_package.cfg` | `;item=optee` → `item=optee, optee.fex` | 使能 OP-TEE 固件加载 |
| `device/config/chips/t527/configs/default/env.cfg` | `keybox_list=hdcpkey,widevine` → `hdcpkey,widevine,ecc_key` | uboot 烧录时识别 `ecc_key` 进 keybox |
| `device/config/chips/t527/configs/myd_lt527_emmc/buildroot/env.cfg` | 同上 | 板级 env |
| `device/config/chips/t527/configs/myd_lt527_emmc/debian/env.cfg` | 同上 | 板级 env(debian 变体) |
| `uboot-board.dts` | `burn_key = <1>`(原有,已确认) | 使能 DragonSN 烧录会话 |

验证:`pack` 后 `pack_out/env.fex` 含 `keybox_list=hdcpkey,widevine,ecc_key`;
`u-boot.fex` 内嵌 dtb `target` 节点 `burn_key = 1`。

## 3. 固件构建与打包

```bash
# 非安全固件(首次使能 OP-TEE)
./build.sh pack
# 产物: out/myd_lt527_emmc_buildroot.img
# 验证:boot_package TOC 含 optee、sunxi.fex 含 linaro,optee-tz 节点

# 安全固件(必须!keybox 加密存储的前提)
./build/createkeys -i t527          # 生成签名密钥组(含 rotpk.bin)
./build.sh pack_secure              # 签名打包
# 产物: out/myd_lt527_emmc_buildroot_secure_v0.img
# SHA256: C47980F6EE9F99DFFBB42766FAC33F13FFC87952D5C05DD14A3A0C0112F22A7D
```

## 4. 烧录过程(顺序不可乱)

### 4.1 烧录安全固件镜像
- PhoenixSuit 全盘擦除烧录 `myd_lt527_emmc_buildroot_secure_v0.img`
- 验证:`sunxi_secure: secure`、`androidboot.verifiedbootstate=green`

### 4.2 烧录 ROTPK(DragonSN v2.7.2)
- 配置:key 类型 **rotpk**(专有类型),选择文件 `rotpk.bin`
- ⚠️ 不能用"固定值"填 hex 文本(uboot 校验 `len != 32` 直接拒绝)
- 验证:`sunxi_rotpk: 1`

### 4.3 烧录 SSK(DragonSN v2.7.2)
- 配置:key 类型 **二进制文件**,目录含 `ssk.bin`,勾 Const
- ⚠️ 此芯片 SSK 出厂已预置,烧录报 `key 'ssk' has been burned already` 属正常,跳过即可
- keybox 加解密正常即证明出厂 SSK 有效

### 4.4 烧录 ecc_key(DragonSN v2.7.2)
- 配置:key 类型 **二进制文件**,目录含 `ecc_key.bin`,勾 Const,key 名 `ecc_key`(匹配 keybox_list)
- 安全固件下 uboot 会 `smc_tee_ssk_encrypt` 加密存储,optee 读取时正确解密

## 5. 板端运行时部署

```sh
# tee-supplicant + libteec
install -m 0755 tee-supplicant /usr/sbin/tee-supplicant
install -m 0644 libteec.so.1.0.0 /usr/lib/libteec.so.1.0.0
ln -sf libteec.so.1.0.0 /usr/lib/libteec.so.1

# TA(724b12aa-6e74-4779-bf3a-1580a076fed3.ta)→ /lib/optee_armtz/
# daemon → /run/media/mmcblk0p6/ecu-ota/bin/token-signer-daemon(UDISK 持久)

# 账号(规范)
addgroup -g 201 ecu-token-client
addgroup -g 200 ecu-token-signer
adduser -D -H -u 200 -G ecu-token-signer -s /bin/false ecu-token-signer

# 启动
tee-supplicant &
token-signer-daemon --uid 200 --gid 200 --socket-gid 201 \
  --client-uid 0 --client-gid 0 --idle-timeout 3600 \
  --endpoint /run/ecu-token-signer/v1.sock &
```

## 6. 端到端验证

```text
init: 0
issue: 0                 # token 签发成功(187B COSE Sign1)
verify: 0 (SUCCESS)      # 公钥验签通过
```

## 7. 烧录文件清单(统一存放)

**`C:\Users\86151\Desktop\token_key\key\`**:

| 文件 | 大小 | 用途 | SHA256 |
| --- | --- | --- | --- |
| `rotpk.bin` | 32B | efuse 烧录(一次性) | `28083495...589431` |
| `ssk.bin` | 32B | efuse 烧录(此芯片出厂已预置,跳过) | `0CC1A885...D3A400` |
| `ecc_key.bin` | 96B | keybox 烧录(d‖x‖y) | `050E66CC...F0DF09` |

配套(不在 key 目录):签名密钥组 `E:\T527\secure-keys-20260816\`(离线保管,丢失=设备失控);
ECU 私钥 PEM `ecc-key-material\can-ota-p256-new.pem`(离线保管,勿入库勿上设备)。

## 8. 关键经验(后续板子)

1. **必须安全固件**(pack_secure)——非安全固件下 uboot 明文写 keybox、optee 按加密读 → 乱码
2. **烧录顺序**:安全镜像 → ROTPK → ecc_key(SSK 出厂预置则跳过)
3. **数据格式**:rotpk 用专有类型;ecc_key/ssk 用"二进制文件"类型选文件
4. **烧录时序**:完全断电 → USB 连接 → 上电(不能刷机模式开机)
5. **密钥配套**:安全启动密钥组必须与 ROTPK 配套,ECU P-256 密钥必须与 daemon 常量配套,两者独立
