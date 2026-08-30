# OP-TEE Token Signer

The OTA signing key is held in the T527 OP-TEE secure world (keybox), never
on the Linux side. `token-signer-daemon` is a TEEC client: it can only ask
the secure world for signatures and read the public key. No PEM private key
file exists on the device.

```text
ota-worker (client) --unix socket--> token-signer-daemon (TEEC client)
                                          | TEEC_InvokeCommand
                                          v
                    OP-TEE secure world: ecdsa-p256-sign TA
                                          | TEE_keybox_load("ecc_key")
                                          v
                        keybox (secure storage, SSK-encrypted)
```

## Components

| Piece | Location | Role |
| ----- | -------- | ---- |
| `ecdsa-p256-sign` TA | `gateway/optee/ecdsa-p256-sign/` | Secure-world signing: loads keypair from keybox at session open, serves `GET_PUBLIC_KEY` and `SIGN_DIGEST` (raw ECDSA P-256, 64-byte r\|\|s) |
| TEEC client | `gateway/apps/token-signer-daemon/token_signer_tee.c` | libteec session + t_cose crypto adapter (SHA-256 stays in REE) |
| daemon | `gateway/apps/token-signer-daemon/token_signer_daemon.c` | COSE Sign1 assembly, socket policy, SO_PEERCRED checks |
| trust anchor | `token_signer_daemon_main.c` | Startup verifies the TA's public key against the compile-time constant; refuses to run on mismatch |

The TA protocol contract lives in
`gateway/optee/ecdsa-p256-sign/ta/include/ecdsa_sign_ta.h` (UUID, command
IDs) and is shared with the daemon.

## Keybox key format

`ecc_key` in the keybox must be the raw P-256 keypair, big-endian:

```text
d (32) || x (32) || y (32)   = 96 bytes
```

The TA validates the blob length and rejects anything else at session open
(`TEE_ERROR_ITEM_NOT_FOUND`), which makes the daemon fail startup loudly
instead of signing with an unexpected key.

## Build

TA (from WSL; syncs into the SDK demo tree and builds with the SDK
`export-ta_arm32` devkit and arm32 toolchain):

```bash
gateway/scripts/build_ta_sdk.sh
# T527_SDK=/path/to/MYD-LT527 overrides the default /mnt/e/... path
```

Output: `<uuid>.ta` in the SDK demo tree `out/ta/`.

Daemon (cross, aarch64):

```bash
gateway/scripts/build_target_wsl.sh build-target-tee
```

Requires the SDK `export-ca` devkit (`libteec.a`, `tee_client_api.h`),
overridable with `T527_TEE_DEVKIT_DIR`.

## Target enablement (board-level, one-time)

These steps change the device image / secure provisioning and are performed
per the Allwinner *Linux 安全开发指南*; they are prerequisites for the
daemon, which fails startup with "secure startup failed" until they hold:

1. **Boot image**: enable OP-TEE in the boot package
   (`device/config/chips/t527/configs/default/boot_package.cfg`, uncomment
   `item=optee, optee.fex`) and repack with `pack_secure`. After boot,
   `/dev/tee0` and `/dev/teepriv0` must exist and the ATF log must show
   `optee_base != 0`.
2. **Kernel**: `CONFIG_TEE=y` and `CONFIG_OPTEE=y` are already in the T527
   defconfigs.
3. **User space**: `tee-supplicant` + `libteec.so.1` on the target (SDK
   `export-ca/sbin/tee-supplicant`, `export-ca/exportlib/`), with
   `tee-supplicant` running (it serves the TA load and secure storage).
4. **TA**: deploy `724b12aa-6e74-4779-bf3a-1580a076fed3.ta` to
   `/lib/optee_armtz/`.
5. **Key provisioning**: burn the 96-byte keypair blob with DragonSN under
   the name `ecc_key`, with `keybox_list` in `env.cfg` including `ecc_key`.
   Burn the SSK first (keybox data is SSK-encrypted). The public key of the
   provisioned key must match the daemon's compile-time constant, or the
   daemon refuses to run.

## DragonSN provisioning procedure

Tool: DragonSN v2.5.1 (`DragonSN.exe` + `DragonKeyConfig.exe`, see the two
PDFs shipped with it). All prerequisites below are verified on the current
board image:

| Prerequisite | Where | Verified |
| ------------ | ----- | -------- |
| `burn_key = 1` | uboot dtb `target` node | yes (dtb value parsed from u-boot.fex) |
| `keybox_list` includes `ecc_key` | env.cfg / env.fex | yes |
| OP-TEE running, `/dev/tee0` | boot image | yes |
| USB driver | installed by APST | — |

Key material (`E:\download\烧号工具dragonsnv2.5.1\ecc-key-material\`):

- `ecc_key.bin` — 96 bytes raw `d \|\| x \|\| y`, big-endian. This is what
  DragonSN burns under the name `ecc_key`.
- `can-ota-p256-new.pem` — the private key. Store offline, never commit it
  to a repository and never put it on the device. The daemon's compile-time
  `expected_public_x963` constant is the matching public key.

### 1. Configure keys (DragonKeyConfig.exe)

1. **Global config**: key class = **secure key** (not private key — secure
   keys go to secure storage/keybox, private keys go to the `private`
   partition). "Set flag" = 0 while developing (allows re-burn; set to 1
   for production to close the burn port after the first burn).
2. **Add key "ecc_key"**: type = binary file, key name = `ecc_key` (must
   match `keybox_list`), source = `ecc_key.bin` (96 bytes).
3. **Add key "ssk"** (or the SSK name from the chip's efuse map): type =
   efuse. Burn the SSK before anything keybox-related: keybox data is
   encrypted by the secure OS with the SSK from efuse. The exact SSK name
   and size come from the efuse map listed by the config tool for this
   chip.

### 2. Burn (DragonSN.exe)

1. Device fully powered off, connect USB, power on (**not** in firmware
   flashing mode).
2. Tool detects the device (auto-burn option works too).
3. Burn SSK first, then `ecc_key`.
4. After success the burn port closes (unless flag=0); re-burning requires
   PhoenixWipe.

### 3. Verify on the board

```sh
/run/media/mmcblk0p6/ecu-ota/bin/token-signer-daemon \
  --uid 200 --gid 201 --socket-gid 201 --client-uid 0 --client-gid 0 \
  --idle-timeout 3 --endpoint /run/ecu-token-signer/v1.sock
```

Success = the daemon survives past startup (public-key check passed) and
exits 0 after the idle timeout. Failure = "token-signer: secure startup
failed" (keybox key missing or public key mismatch).

## Deployment

`scripts/deploy_ecu_ota_runtime_adb.ps1` publishes the three binaries only.
The signer key file (`/etc/ecu-ota/trust/can-ota-p256-private.pem`) is
obsolete: the old daemon loaded it, the OP-TEE daemon never touches it.
Remove it from the device and never redeploy it.

## Signing path (unchanged contract)

The daemon protocol (CBOR over `SOCK_SEQPACKET`, SO_PEERCRED policy,
COSE Sign1 ES256 token format, claims) is unchanged; only the private-key
source changed from a PEM file to the secure world.
