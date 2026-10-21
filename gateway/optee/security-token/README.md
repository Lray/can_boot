# SecurityAccess token TA

Protocol-bound SecurityAccess token trusted application for T527 OP-TEE.

The P-256 keypair is provisioned into the keybox (`ecc_key`) by DragonSN at
manufacturing time and is loaded inside the secure world only
(`TEE_keybox_load`). The normal world never sees private key material; it can
only submit a four-byte SecurityAccess seed and receive a complete COSE Sign1
token. It can also read the public key for startup trust-anchor validation.

## Keybox key format

The keybox `ecc_key` blob must be the raw P-256 keypair

```text
d (32 bytes) || x (32 bytes) || y (32 bytes)
```

big-endian, exactly 96 bytes. Keys in any other format are rejected at session
open (`TEE_ERROR_ITEM_NOT_FOUND`).

## Commands

| Command | Input | Output |
| --- | --- | --- |
| `TOKEN_TA_CMD_GET_PUBLIC_KEY` | none | 64-byte x \|\| y |
| `TOKEN_TA_CMD_ISSUE_TOKEN` | non-zero 4-byte seed | complete COSE Sign1 token |

The TA constructs the fixed business claims, COSE Sig_structure, SHA-256
digest, and ES256 signature internally. It exposes no general-purpose signing
operation.

## Build

The TA builds with the SDK TA devkit (`export-ta_arm32`), the same toolchain
and signing key as the SDK demos:

```bash
# from the SDK root (after ./build.sh board selection and
# platform/allwinner/security/optee$ ./build.sh config)
./build.sh {SDK}/platform/allwinner/security/optee/demo/security-token
```

The repository-local `Makefile` accepts explicit `TA_DEV_KIT_DIR` and
`CROSS_COMPILE_TA` values for a standalone SDK build. Output:

```text
out/ta/724b12aa-6e74-4779-bf3a-1580a076fed3.ta
```

## Install on the target

```bash
cp <uuid>.ta /lib/optee_armtz/
# tee-supplicant, libteec.so.1 and /dev/tee0 must be present (OP-TEE enabled
# in the boot image, see gateway/docs/optee-security-token.md)
```

## Provisioning

Burn the keypair blob with DragonSN under the name `ecc_key`. The boot
`env.cfg` `keybox_list` must include `ecc_key` so uboot stores it in the
keybox. Burn the SSK first; keybox data is encrypted with the SSK.
