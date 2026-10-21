# Gateway CAN Bring-Up Checklist

## RAW CAN

Preconditions:

- Target kernel has SocketCAN enabled.
- `awlink0` exists on the target board.
- Root filesystem includes `iproute2`.
- Root filesystem includes `can-utils` with `candump`.
- CAN bus is terminated correctly.
- MCU side is configured for classic CAN at `500000` bps.
- The tested MCU has a commissioned Node-ID `N` in `1..127`.
- MCU firmware periodically transmits heartbeat ID `0x700 + N`.
- MCU firmware responds to request ID `0x600 + N` on response ID `0x580 + N`.

Gateway commands:

```bash
cd /opt/can-ota-gateway
./scripts/setup_can0.sh awlink0 500000
./scripts/verify_raw_can.sh awlink0 .
```

Required evidence:

- `ip -details link show awlink0` after setup.
- `candump -tz awlink0` line containing heartbeat `0x700 + N` with payload `05` (DLC `1`).
- Heartbeats arrive every `1000 ms`; a consumer records the last valid receive time and considers the MCU offline after more than `3000 ms` without one.
- `raw_can_smoke` or `cansend` evidence that the gateway sent on `0x600 + N`.
- `candump -tz awlink0` evidence that only the selected MCU responded on `0x580 + N`.
- `ip -details -statistics link show awlink0` after the smoke test, showing `ERROR-ACTIVE`, `berr-counter tx 0 rx 0`, `bus-errors 0`, `error-warn 0`, `error-pass 0`, `bus-off 0`, and RX/TX errors `0`.
- Final script line: `RAW CAN PASS`.

Seeing only the local request is not sufficient. RAW CAN success requires the
selected node's heartbeat and response.

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

- single-frame ISO-TP request receives the matching `0x580 + N` response
- multi-frame ISO-TP request receives the matching `0x580 + N` response
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

- positive `0x10 02` programming session response
- positive `0x3E 00` TesterPresent response
- positive `0x22` reads for `0xF1F0` through `0xF1F4` (and `0xF1F5` for factory identity)
- one negative response path, preferably unsupported DID `0xFFFF`
- `0x78 ResponsePending` is unit-tested as a generic UDS transaction behavior; OTA erase
  progress is validated through the `0x31 FF00` routine result instead
- link remains `ERROR-ACTIVE` with stable error counters

## OTA integration

Do not invoke individual download, pre-check, or reset
services from a board smoke tool. The only integration path is:

```text
SWUpdate -> mcu-updater -> official ASCII -> LSS_gateway
                       <- Node-ID <- Fastscan/select/query/deselect
                       -> mcu_update_run_image -> ota_executor
```

Required evidence:

- `LSS_gateway.service` and `mcu-updater.service` are active; the updater
  performs Fastscan/configuration and identity select/query/deselect in one
  exclusive session, obtaining a valid Node-ID before starting OTA.
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
- Only `LSS_gateway` initializes the official LSS Master and Gateway ASCII modules
  and runs the CAN receive/timing loop. The updater requests Fastscan/configuration,
  identity selection, inquiry, and deselection in one exclusive socket session
  before opening the ISO-TP OTA channel.
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
- wrong or repeated BSC returns upstream NRC `0x24`, resets the transfer state,
  and the gateway stops the stream without retrying the block
- a malformed or wrongly signed image may pass transport pre-check only if its
  format is valid; after reset MCUboot must reject it and retain the previous slot
- confirm-missing rollback evidence captures the test-boot version and
  `F1F4=self-check-failed`, then captures the former slot and version after a
  second reset
- link remains `ERROR-ACTIVE` with stable error counters
