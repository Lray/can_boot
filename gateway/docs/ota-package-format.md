# MCU update package contract

The SWU contains one remote artifact: the standard MCUboot `image.bin`.
Target identity is not embedded in or appended to that image.

## Signed target selection

The signed `sw-description` selects the MCU through the Remote Handler
`data` attribute:

```text
mcu-v1-VVVVVVVV-PPPPPPPP-RRRRRRRR-SSSSSSSS
```

The four uppercase hexadecimal fields are the CANopen LSS identity in official
order: vendor-ID, product-code, revision-number, serial-number. This is the
single package-side identity representation.

SWUpdate verifies the signed description and the artifact SHA-256 before the
official Remote Handler completes installation. The handler itself is
unmodified. The production SWUpdate 2019.11 build must set its official
`CONFIG_SOCKET_REMOTE_HANDLER_DIRECTORY` to
`/run/mcu-update/remote-handler/`; the service also keeps `TMPDIR` on that same
directory. Normal handler selection then connects to the identity-specific
socket already bound by `mcu-updater`.

At startup, `mcu-updater` loads the persistent LSS identity-to-Node-ID
registry and binds one endpoint for each registered MCU. Its single event loop
accepts and executes only one update transaction at a time. The selected
identity determines the Node-ID, which only configures the ISO-TP transport IDs:

```text
request  = 0x600 + Node-ID
response = 0x580 + Node-ID
```

UDS remains independent of those CAN identifiers.

## Runtime checks

Before SecurityAccess or any download operation, the Gateway reads
`DID_LSS_IDENTITY` from the selected MCU and requires an exact 16-byte match
with the signed target identity. It repeats the same check after reset, together
with the active slot, MCUboot version and confirmation result. A missing or
corrupt registry, unknown identity, busy CAN interface, DID mismatch or unstable
snapshot fails closed before programming.

`package_store` receives only `image.bin`, enforces the slot-size limit,
derives its SHA-256 while receiving, writes it with `fsync` and atomically
publishes `package-input-v1/image.bin`. The update engine independently derives
the image size, digest and version from that file. MCUboot remains responsible
for the image signature, security counter, boot and rollback decisions.

## Release builder

`scripts/build_mcu_hawkbit_swu_wsl.sh` accepts an already signed `image.bin`
created only with `E:\\T527\\can_boot\\tools\\image.py`. It requires a
canonical target identity and uses official `swugenerator` with the SWU signing
key under `E:\\T527\\can_boot\\key`. It does not create or modify the MCUboot
image. The resulting SWU is checked with the target's official `swupdate -c`.

The obsolete raw OpenSSL/cpio wrapper was removed; no compatibility package
format is retained.
