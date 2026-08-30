# OTA key management and OP-TEE seed-signing provisioning

## Independent key domains

| Domain | Private material location | Public/verifier location | Sole use |
| --- | --- | --- | --- |
| MCUboot image signing | Offline release key | STM32 MCUboot trust configuration | Sign the final `image.bin` with MCUboot `imgtool`. |
| SWU container signing | Offline RSA-PSS release key | `/etc/ecu-ota/trust/swu-release.pem` | Sign `sw-description`; SWUpdate verifies it. |
| ECU SecurityAccess signing | `ecc_key` in T527 keybox / OP-TEE secure world | ECU P-256 public key and daemon compiled key | Sign the fresh MCU seed challenge. |
| T527 secure boot and keybox root | Secure boot keys, ROTPK efuse, SSK efuse | Boot ROM / secure boot chain | Authenticate secure firmware and protect keybox storage. |

These keys are unrelated. A key from one row must never be used in another
row. No private value, seed, target token, Management password, or raw UDS
SecurityAccess frame belongs in the repository, a unit file, command history,
or logs. Any private material recorded in old plaintext development notes must
be treated as exposed and rotated before production.

## What OP-TEE signs

The MCU returns a fresh UDS SecurityAccess seed. The gateway never turns the
seed into a password and never loads the P-256 private key:

```text
MCU seed challenge
  -> gateway OTA worker
  -> token-signer-daemon SOCK_SEQPACKET request
  -> OP-TEE ECDSA TA: TEE_keybox_load("ecc_key")
  -> raw P-256 signature
  -> COSE Sign1 token
  -> MCU verifies it and permits the existing download protocol
```

`ecc_key` is raw big-endian `d(32) || x(32) || y(32)`, exactly 96 bytes. It is
not PEM, DER, an MCUboot key, or a SWU key. The TA accepts a SHA-256 digest and
returns raw `r || s`; `token-signer-daemon` validates the TA public key at
startup and rejects a mismatched keybox.

## Correct production provisioning path

This uses the selected SDK configuration and the Allwinner production
DragonSN/DragonKeyConfig toolchain. A reference-board development sequence is
not a factory procedure.

### 1. Verify the selected build configuration

For the current Buildroot product, inspect the actual selected files before
each secure build:

- `device/config/chips/t527/configs/myd_lt527_emmc/buildroot/env.cfg` must list
  `ecc_key` in `keybox_list`.
- `device/config/chips/t527/configs/default/boot_package.cfg` must contain
  `item=optee, optee.fex`.
- `device/config/chips/t527/configs/myd_lt527_emmc/uboot-board.dts` must have
  `burn_key = <1>`.

Do not change only `configs/default`: the product variant owns its own
`env.cfg`. In the current SDK, Buildroot and Debian include `ecc_key`, while
the Yocto variant does not. If Yocto is selected, change its own `env.cfg`,
review the exact diff, and rebuild that selected image.

Create the production image with `build.sh pack_secure` and verify that OP-TEE
is present in the boot package. A non-secure `pack` image is not a production
keybox image: U-Boot keybox writing and OP-TEE secure keybox reading will not
use the same protection semantics.

### 2. Prepare offline inputs

In an approved offline/HSM process, prepare the MCUboot signing key, SWU
RSA-PSS key, `ecc_key.bin`, secure-boot certificate chain, matching
`rotpk.bin`, and the chip-lot SSK policy. Keep private material outside the
SDK tree. Verify that `ecc_key.bin` is 96 bytes and that its public `x || y`
matches the daemon's compiled trust constant. Record key IDs, public
fingerprints, operator, board serial, tool version, and package hashes in the
controlled manufacturing record, never private bytes.

### 3. Burn with DragonSN

1. Flash the signed secure production image first.
2. Program `rotpk.bin` using DragonSN's dedicated **ROTPK** key type. It is a
   binary file, not pasted hexadecimal text. Confirm its public fingerprint
   matches the secure image chain before the irreversible efuse operation.
3. Follow the chip-lot SSK plan. If the factory SSK is already fused, record
   the tool's “already burned” result and do not overwrite it. If an approved
   SSK burn is required, it must happen before every keybox write.
4. Program `ecc_key.bin` as a **binary keybox file** named exactly `ecc_key`.
   The name must match `keybox_list`; do not use PEM, rename it, or copy it to
   normal-world storage.
5. Lock the burn policy only after documented functional checks and recovery
   approval are complete.

Power the board fully off, connect the production USB path, then power it
normally for the burn session. Do not mistake this for PhoenixSuit firmware
flashing mode. The SDK OP-TEE sample documents `keybox_ca` for development;
it does not make a normal-world helper equivalent to DragonSN's production
ROTPK/SSK/keybox flow. Use DragonSN for factory provisioning.

### 4. Deploy and validate the runtime

Deploy only public/runtime artifacts: OP-TEE device nodes, `tee-supplicant`,
matching `libteec`, TA
`724b12aa-6e74-4779-bf3a-1580a076fed3.ta` in `/lib/optee_armtz/`, the signer
service, and the SWU public trust PEM. The functional proof is that the signer
opens the TA, obtains the expected public key, creates its peer-checked socket,
and successfully issues a test token. Preserve pass/fail and public
fingerprints only; never log a seed, signature, token, or key material.

## Release gates

- `can/scripts/make_ecu_mcuboot_bundle.py` is the sole image builder. It calls
  MCUboot `imgtool`, verifies header/TLV/trailer invariants, and creates the
  final full-slot `image.bin`.
- `gateway/scripts/build_ecu_hawkbit_swu_wsl.sh` is the sole SWU builder. It
  follows official SWUpdate RSA-PSS + CPIO-CRC ordering and validates with
  `swupdate -c`.
- The Remote Handler's required size and SHA-256 bind this exact final
  `image.bin`. They supplement the SWU outer signature and MCUboot's final
  signature/security-counter decision; they never replace either layer.
