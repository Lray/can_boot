# Gateway CAN Bring-Up Checklist

## RAW CAN

Preconditions:

- Target kernel has SocketCAN enabled.
- `awlink0` exists on the target board.
- Root filesystem includes `iproute2`.
- Root filesystem includes `can-utils` with `candump`.
- CAN bus is terminated correctly.
- MCU side is configured for classic CAN at `500000` bps.
- MCU firmware periodically transmits heartbeat ID `0x700`.
- MCU firmware echoes or otherwise responds to gateway request ID `0x7E0` on response ID `0x7E8`.

Gateway commands:

```bash
cd /opt/can-ota-gateway
./scripts/setup_can0.sh awlink0 500000
./scripts/verify_raw_can.sh awlink0 .
```

Required evidence:

- `ip -details link show awlink0` after setup.
- `candump -tz awlink0` line containing MCU heartbeat standard frame `700#05` (DLC `1`).
- Heartbeats arrive every `1000 ms`; a consumer records the last valid receive time and considers the MCU offline after more than `3000 ms` without one.
- `raw_can_smoke` or `cansend` evidence that gateway sent standard CAN ID `7E0` with payload `11 22 33 44 55 66 77 88`.
- `candump -tz awlink0` line containing MCU response standard CAN ID `7E8` with payload `11 22 33 44 55 66 77 88`.
- `ip -details -statistics link show awlink0` after the smoke test, showing `ERROR-ACTIVE`, `berr-counter tx 0 rx 0`, `bus-errors 0`, `error-warn 0`, `error-pass 0`, `bus-off 0`, and RX/TX errors `0`.
- Final script line: `RAW CAN PASS`.

Seeing only the local `7E0` request in `candump` is not sufficient. RAW CAN success requires evidence from the MCU side: heartbeat `700` and response `7E8`.

## Next validations

Do not start OTA download services before these pass in order:

1. `RAW CAN`
2. `ISO-TP`
3. `Minimal UDS`

## ISO-TP

Do not start UDS until ISO-TP transport is proven.

Gateway command:

```bash
cd /opt/can-ota-gateway
./scripts/verify_isotp.sh awlink0
```

Required evidence:

- single-frame ISO-TP request receives matching `0x7E8` response
- multi-frame ISO-TP request receives matching `0x7E8` response
- flow-control profile is recorded as `BS=8`, `STmin=2 ms`
- timeout handling is explicit; a missing response fails the script
- link remains `ERROR-ACTIVE` with stable error counters

## Minimal UDS

Do not start OTA services until minimal UDS passes.

Gateway command:

```bash
cd /opt/can-ota-gateway
./scripts/verify_uds.sh awlink0 .
```

Required evidence:

- positive `0x10 03` extended diagnostic session response
- positive `0x10 02` programming session response
- positive `0x3E 00` TesterPresent response
- positive `0x22` reads for `0xF1F0` through `0xF1F4` (and `0xF1F5` for LSS identity)
- one negative response path, preferably unsupported DID `0xFFFF`
- `0x78 ResponsePending` is unit-tested as a generic UDS transaction behavior; OTA erase
  progress is validated through the `0x31 FF00` routine result instead
- link remains `ERROR-ACTIVE` with stable error counters

## OTA integration

Do not invoke individual download, pre-check, or reset
services from a board smoke tool. The only integration path is:

```text
SWUpdate -> mcu-updater -> mcu_update_run_job -> ota_executor
```

Required evidence:

- positive `0x31 01 FF 00` followed by `0x31 03 FF 00` result `0x00000000`
- positive `0x34` response with `maxNumberOfBlockLength = 258`, without `0x78`
- `0x36` responses match block sequence counters starting at `0x01`
- payload size is `256 B` except the final short block if any
- positive empty-payload `0x37` response
- at least one NRC path for invalid length/range or wrong block sequence
- link remains `ERROR-ACTIVE` with stable error counters

## Architecture Check

Before moving to another validation, confirm the new code keeps these boundaries:

- SocketCAN/device access is isolated from UDS/updater policy.
- ISO-TP exposes transport operations rather than leaking socket setup into UDS.
- UDS client logic is separate from package parsing and transfer scheduling.
- Shared protocol constants are read from `src/profile.h`.
- No board tool bypasses `ota_executor` to construct a partial OTA flow.

## HIL failure and rollback closure

Do not declare overall OTA validation PASS until the following cases have been
run against a real MCU. No host fake is a substitute for these checks.

Required evidence:

- an interrupted transfer is restarted by erasing the inactive slot and sending
  the image again from block sequence counter `0x01`
- wrong BSC returns NRC `0x73`, and the gateway stops the stream
- a malformed or wrongly signed image may pass transport pre-check only if its
  format is valid; after reset MCUboot must reject it and retain the previous slot
- confirm-missing rollback evidence captures the test-boot version and
  `F1F4=self-check-failed`, then captures the former slot and version after a
  second reset
- link remains `ERROR-ACTIVE` with stable error counters
