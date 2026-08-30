# ecdsa-p256-sign TA

ECDSA-P256 signing trusted application for the T527 OP-TEE secure world.

The P-256 keypair is provisioned into the keybox (`ecc_key`) by DragonSN at
manufacturing time and is loaded inside the secure world only
(`TEE_keybox_load`). The normal world never sees private key material; it can
only request signatures and read the public key.

## Keybox key format

The keybox `ecc_key` blob must be the raw P-256 keypair

```text
d (32 bytes) || x (32 bytes) || y (32 bytes)
```

big-endian, exactly 96 bytes. Keys in any other format are rejected at session
open (`TEE_ERROR_ITEM_NOT_FOUND`).

## Commands

| Command                        | Input (param 0)  | Output (param 1) |
| ------------------------------ | ---------------- | ---------------- |
| `ECDSA_SIGN_CMD_GET_PUBLIC_KEY` | —                | 64-byte x \|\| y  |
| `ECDSA_SIGN_CMD_SIGN_DIGEST`   | 32-byte digest   | 64-byte raw r\|\| s |

`SIGN_DIGEST` signs the caller-supplied SHA-256 digest with raw ECDSA
(`TEE_ALG_ECDSA_P256`, no internal hashing).

## Build

The TA builds with the SDK TA devkit (`export-ta_arm32`), the same toolchain
and signing key as the SDK demos:

```bash
# from the SDK root (after ./build.sh board selection and
# platform/allwinner/security/optee$ ./build.sh config)
./build.sh {SDK}/platform/allwinner/security/optee/demo/ecdsa-p256-sign
```

or standalone, see `gateway/scripts/build_ta_sdk.sh`. Output:

```text
ta/out/ta/724b12aa-6e74-4779-bf3a-1580a076fed3.ta
```

## Install on the target

```bash
cp <uuid>.ta /lib/optee_armtz/
# tee-supplicant, libteec.so.1 and /dev/tee0 must be present (OP-TEE enabled
# in the boot image, see gateway/docs/optee-token-signer.md)
```

## Provisioning

Burn the keypair blob with DragonSN under the name `ecc_key`. The boot
`env.cfg` `keybox_list` must include `ecc_key` so uboot stores it in the
keybox. Burn the SSK first; keybox data is encrypted with the SSK.
