# ECU MCUboot Runtime Bundle

`scripts/make_ecu_mcuboot_bundle.py` is the canonical release tool for both
Gateway OTA packages and Boot YMODEM provisioning images. Its name and
contract describe the product responsibility and do not depend on a delivery
phase or validation milestone.

## Slot layout

| Slot | Slot base | Payload/vector base | Trailer offset |
| --- | --- | --- | --- |
| 0 | `0x08010000` | `0x08010200` | `0x1FFC0` |
| 1 | `0x08030000` | `0x08030200` | `0x1FFC0` |

The raw `Can.bin` must be linked for the selected slot. The tool adds the
`0x200`-byte MCUboot header, signs the image, verifies the protected security
counter and signature TLVs, and produces a full `0x20000`-byte slot image.

## Gateway OTA package

Generate the payload with the matching CMake slot preset, then create the
package. For slot 1:

```powershell
cmake --preset Debug-slot1
cmake --build --preset Debug-slot1

python E:\T527\can_boot\can\scripts\make_ecu_mcuboot_bundle.py `
  --target-slot 1 `
  --version 1.2.0 `
  --security-counter 11 `
  --payload E:\T527\can_boot\can\build\Debug-slot1\Can.bin `
  --out-dir E:\T527\can_boot\can\build\ota-slot1 `
  --key E:\Simple_ST\Boot\keys\root-ec-p256.pem `
  --imgtool E:\Python314\Scripts\imgtool.exe
```

The ordinary OTA image contains a pending trailer prepared by the release
tool:

| Slot-relative range | OTA value |
| --- | --- |
| `0x1FFC0..0x1FFCF` | erased `0xFF` |
| `0x1FFD0..0x1FFDF` (`copy_done`) | erased `0xFF` |
| `0x1FFE0..0x1FFEF` (`image_ok`) | erased `0xFF` |
| `0x1FFF0..0x1FFFF` | MCUboot trailer magic |

The release tool has already written the magic into the delivered full-slot
image. The ECU streams the received image bytes directly into the inactive
slot; the application does not manufacture or validate the pending trailer.
After the new application passes its startup self-check, it writes only
`image_ok=1`.

Outputs include `image.bin`, `package.info`, `package.source`,
`package.sha256`, and a copy of the slot-specific raw payload. The image
SHA-256 is calculated from the final image including the pending trailer
magic; gateway derives size, digest, and version from this `image.bin` alone.

## Boot YMODEM provisioning image

Initial slot provisioning requires an already-confirmed Direct-XIP image:

```powershell
python E:\T527\can_boot\can\scripts\make_ecu_mcuboot_bundle.py `
  --target-slot 0 `
  --version 1.2.0 `
  --security-counter 11 `
  --payload E:\T527\can_boot\can\build\Debug-slot0\Can.bin `
  --out-dir E:\T527\can_boot\can\build\ymodem-slot0 `
  --key E:\Simple_ST\Boot\keys\root-ec-p256.pem `
  --imgtool E:\Python314\Scripts\imgtool.exe `
  --provisioning
```

This mode writes trailer magic, `copy_done=1`, and `image_ok=1`, and emits
only `image.bin` so a pre-confirmed provisioning image cannot be mistaken for
a Gateway OTA package input.

## Release checks

The tool fails closed unless:

- the vector table matches the selected slot and STM32U5 SRAM layout;
- the signed extent stops before `0x1FFC0`;
- MCUboot header version and security counter match the requested values;
- protected and standard TLVs contain the required entries;
- the final trailer exactly matches pending or confirmed mode.
