# MCU identity and CANopen persistence integration

## Fixed memory contract

The STM32U5A9 linker template identifies a 4 MiB part, while the current boot
Flash map deliberately describes the lower 2 MiB and places its persistent
tail below `0x08200000`. This device/bank choice still needs comparison with
the separately maintained bootloader. Within that current map, 8 KiB erase
pages are reserved as follows:

| Region | Address | Size |
|---|---:|---:|
| Communication configuration | `0x081F2000-0x081F3FFF` | `0x2000` |
| Factory identity | `0x081F4000-0x081F5FFF` | `0x2000` |
| Boot security state | `0x081F6000-0x081F9FFF` | `0x4000` |
| Download journal | `0x081FA000-0x081FDFFF` | `0x4000` |
| Boot user | `0x081FE000-0x081FFFFF` | `0x2000` |

The two application slots are `0x08010000-0x0802FFFF` and
`0x08030000-0x0804FFFF`; their MCUboot trailers are inside those slots. They do
not overlap the reserved tail pages. The bootloader source is not part of this
repository, so its independent partition definition and production erase
ranges still require comparison before release. Mass erase is not a valid
normal provisioning or update operation after identity has been programmed.
The linker rejects every image range that reaches `0x081F2000`.

## Factory identity

The on-Flash payload is exactly four little-endian `uint32_t` values in this
order: Vendor-ID, product code, revision number, serial number. No format word,
CRC, padding record, UID derivative, or firmware version is stored. Runtime
firmware exposes read-only access through `FactoryIdentity_Read()`; the erased
16-byte value is reported as unprovisioned.

`scripts/make_factory_identity_image.py` creates a 16-byte binary and an Intel
HEX file containing one 16-byte data record at `0x081F4000`. The production
system must allocate the serial number, erase only the identity page, program
the HEX record, read back the same 16 bytes, and compare them with the binary
before accepting the unit. The script deliberately has no STM32 UID input.

An example production sequence is:

```powershell
python scripts\make_factory_identity_image.py `
  --vendor-id 0x11223344 --product-code 0x55667788 `
  --revision-number 1 --serial-number 123456 `
  --out-dir production-output
STM32_Programmer_CLI.exe -c port=SWD -w production-output\factory-identity.hex -v
STM32_Programmer_CLI.exe -c port=SWD -u 0x081F4000 16 production-output\factory-identity-readback.bin
```

The production controller must compare `factory-identity-readback.bin` byte
for byte with `factory-identity.bin` in the output directory. Page erase policy
remains the production controller's responsibility; it must never expand this
into a chip-wide erase.

## Upstream version status

The CANopenNode LSS and generic storage files match upstream commit
`9b8beed8367241e96ac03f916cd5a500bcb2cf23`. `CO_LSSslave.c/.h` are compiled
unchanged through the project driver ABI adapter. The complete CANopen stack,
object dictionary, NMT, SDO, and PDO modules are deliberately not linked.

The clarified communication-configuration requirement does not need page
rotation, wear levelling, or recovery of an interrupted update. X-CUBE-EEPROM
is therefore not used. One logical `uint32_t` packs Node-ID in bits 0-7 and the
fixed 500 kbit/s value in bits 8-23; bits 24-31 must be zero. Other stored
bitrate values are invalid. STM32U5 programs a
128-bit quadword, so the remaining twelve physical bytes are `0xFF` padding,
not fields. An interrupted erase/program operation may leave the configuration
invalid; cold start then uses Node-ID `0xFF` and the product default 500 kbit/s.

The product does not register the optional LSS bit-timing check or activation
callbacks. Bitrate configuration requests are therefore not acknowledged, as
defined by the upstream LSS slave implementation; FDCAN timing remains the
CubeMX-generated 500 kbit/s configuration.

The Linux target must use unmodified `CO_storageLinux.c/.h` from a fixed
CANopenLinux commit. Its implementation uses CRC and temporary/old files but
does not perform the required `fsync()` operations. The project integration
must recover `.tmp`/`.old` files before initialization and sync the committed
file and parent directory after critical stores; those guarantees must not be
claimed until that layer and its power-loss tests exist.

## Gateway Phase 0 findings

The SDK's current Buildroot partition table for `myd_lt527_emmc` defines
`boot-resource`, `env`, `boot`, `rootfs`, and `recovery` only. It does not
define `UDISK`. At the same time, its startup script attempts to format
`/dev/by-name/UDISK`, and its udev rule mounts discovered filesystems under
`/run/media/<block-device-name>`. Consequently,
`/run/media/mmcblk0p6/mcu-update/state/` is only a candidate: neither the
existence of `mmcblk0p6` nor its preservation contract follows from the current
partition table. A release must add or select an explicit persistent data
partition, mount it deterministically, and exclude it from production flashing
and every SWUpdate description before the storage integration is enabled.

The checked A/B SWUpdate descriptions write bootloader components plus only
the selected `bootA`/`rootfsA` or `bootB`/`rootfsB` targets; they do not name a
data partition. This is useful evidence but is not sufficient without a
matching deployed partition table and an on-device mount check.

The MCU remains a fixed raw CAN/ISO-TP/UDS product and does not run the complete
CANopenNode stack. Its standalone official LSS Slave is the deliberate
exception: it shares the existing CAN driver, loads the complete factory
identity before initialization, and owns only discovery and communication
configuration.

The application calls `CO_LSSslave_init()`, `CO_LSSslave_process()` and
`CO_LSSslave_initCfgStoreCall()` directly. The STM32-adapted
`CO_storageBlank` loads and stores only the Node-ID. Its private Flash record
also carries the fixed 500 kbit/s value so stale records from another product
configuration are rejected; bitrate is not exposed as a configurable storage
API. When the upstream process function requests communication reset after assigning an
initially unconfigured Node-ID, the application stops CAN, reinitializes only
the LSS object and its two CAN buffer registrations, then resumes CAN. Pending
values are retained in RAM and are not reloaded from Flash. ISO-TP, UDS,
download and logging state are not reinitialized by this transition.

For an already configured node, the upstream LSS Slave does not request an
autonomous communication reset; a changed Node-ID therefore remains pending
until the product's normal device-reset path runs and reloads the stored value.
