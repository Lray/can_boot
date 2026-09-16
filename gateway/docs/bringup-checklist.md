# Gateway multi-MCU board bring-up

This checklist requires the target SDK-built Gateway programs and
STM32CubeIDE-built MCU firmware. Host tests are not part of this acceptance.

## 1. Physical CAN

For every MCU, verify 500 kbit/s Classic CAN, correct termination and an
ERROR-ACTIVE interface with stable error counters. An unconfigured MCU must
expose only official LSS traffic; it must not emit UDS, heartbeat or log frames.

## 2. Commission identities

Stop `mcu-updater.service` before commissioning because commissioning and
updating intentionally share one per-interface lock.

For each MCU, use either:

```sh
gateway-lss-master fastscan awlink0 <node-id>
gateway-lss-master select awlink0 <vendor> <product> <revision> <serial> <node-id>
```

Required evidence:

- the tool prints the four-field identity and previous Node-ID;
- the selected Node-ID is 1..127 and unique on the interface;
- after store and deselect, reselect/inquire returns that active Node-ID;
- `/var/lib/mcu-update/devices/awlink0.bin` is created with root ownership and
  is unchanged after a failed/conflicting registration;
- duplicate identity, duplicate Node-ID, corrupt registry and concurrent tool
  invocation all fail closed.

Restart `mcu-updater.service` after the last registration. Confirm one socket
per identity exists under `/run/mcu-update/remote-handler/`.

## 3. Node-based transport

For each registered Node-ID `n`, capture:

- UDS request `0x600+n` and response `0x580+n`;
- heartbeat `0x700+n`, DLC 1, data `05`, period 1000 ms;
- optional logs only on `0x680+n`.

Run the board smoke programs with explicit Node-ID. Read
`DID_LSS_IDENTITY (0xF1A9)` and require the expected four BE32 fields.
Also verify sessions `0x10 03` and `0x10 02`, TesterPresent `0x3E 00`,
version/slot/confirmation DIDs, one unsupported DID response, multi-frame
transport, BS=8 and STmin=2 ms.

## 4. Signed target selection

Build separate SWUs from the same valid `image.bin` for two registered
identities. Inspect each signed `sw-description`: its `data` value must be
`mcu-v1-VENDOR-PRODUCT-REVISION-SERIAL`; `image.bin` must be byte-identical
between packages.

Assign one SWU through HawkBit. Required evidence:

- only the selected MCU enters UDS programming;
- the Gateway reads and matches F1A9 before SecurityAccess;
- the other MCU continues its own heartbeat and receives no download request;
- the update is serialized and the per-interface lock rejects commissioning;
- after reset, F1A9, target slot, full MCUboot version and confirmation result
  form one stable post-reset snapshot.

Repeat with the other identity.

## 5. Failure and recovery

Before declaring board acceptance, exercise:

- signed package targeting an identity absent from the registry;
- stale registry mapping where the Node-ID answers with another F1A9 identity;
- malformed registry and missing registry;
- wrong transfer sequence (NRC 0x73);
- interruption followed by journal-matched resume;
- malformed or wrongly signed MCUboot image rejected after reset;
- confirmation failure followed by MCUboot rollback;
- power loss while persisting a new registration and during image reception.

No failure may erase or program an MCU whose observed identity differs from the
signed target identity.
