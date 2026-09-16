# Multi-MCU Update Chain Review

## Decision

The product is a T527 Linux Gateway with multiple registered STM32 MCUs on one
CAN interface. A signed LSS identity selects one target; the process serializes
all updates and does not implement a generic parallel OTA scheduler.

```text
systemd/network                token-signer-daemon
       |                              |
       v                              v
SWUpdate Suricatta             OP-TEE signing key
       |
       v
Remote Handler (ZeroMQ)
       |
       v
mcu-updater (one production process, one endpoint per registered identity)
  1. receive one bounded image.bin
  2. enforce announced size and derive SHA-256
  3. fsync and atomically publish
  4. validate MCUboot metadata
  5. authorize the UDS flashing session
  6. resolve Node-ID, verify DID identity and configure ISO-TP IDs
  7. reconnect and confirm the running image
       |
       v
MCUboot: signature / security counter / A-B boot / rollback
```

## What was removed

The former production receiver launched `former helper process` as a second
process and interpreted its OTA state enum as a Unix exit code. That split did
not create an independent trust boundary: both processes ran as the same
identity, consumed the same job directory, and together implemented one
business operation. It also made a confirmed lifecycle state (`7`) disagree
with the parent's success convention (`0`).

The production updater now calls `mcu_update_run_job()` directly. The function
returns `0` only after `OTA_STATE_CONFIRMED`; a separate result field retains
the terminal lifecycle state for diagnostics. Package-policy, signer-policy,
transport, execution, and internal failures use distinct non-zero codes.

## Boundaries retained

- SWUpdate remains the only HawkBit/DDI client and signed-SWU verifier.
- `PackageStore` remains the only streamed-image publication owner.
- `mcu_update_run_job()` joins trusted input, registry-selected transport,
  signer client, observed MCU identity, and the OTA executor.
- `ota_executor` remains the only OTA lifecycle state machine.
- The OP-TEE daemon remains a separate least-privilege key service because
  isolation of the private signing key is a real security boundary.
- MCUboot remains the final image authenticity and rollback authority.

## Diagnostic path

`mcu-updater-direct` exists for board bring-up, HIL, and production-line
diagnostics. It accepts an already published secure job directory and calls the
same application service. Production deployment scripts do not install it,
and it must not be described as a second production update architecture.

## Deliberate constraints

The CAN interface is fixed. Official CANopen LSS assigns Node-IDs and the
project derives UDS/log/heartbeat identifiers from them. Updates remain serial;
there is no parallel scheduler, SDO stack or SWUpdate `hardware-compatibility`.
