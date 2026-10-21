# MCU Update Chain Review

## Decision

The current product is a T527 Linux gateway and one resource-constrained MCU.
Each transaction selects the target by its factory identity and runs serially.
Multi-MCU production requires recoverable Node-ID allocation and multi-node HIL.

```text
systemd/network                 tee-supplicant
       |                              |
       v                              v
SWUpdate Suricatta              OP-TEE token TA
       |
       v
Remote Handler (ZeroMQ)
       |
       v
mcu-updater (resident image receiver, direct libteec client)
  1. receive one bounded image.bin
  2. enforce announced reception size
  3. buffer the current transaction in an anonymous memfd
  4. send official ASCII commands to LSS_gateway through one exclusive Unix socket session
     -> Fastscan/configuration/identity selection/inquiry/deselection -> active Node-ID
  5. require Node-ID 1..127; call the OTA function and validate MCUboot metadata
  6. verify the UDS identity, authorize, and transfer through that Node-ID's ISO-TP IDs
  7. reconnect and confirm the running image
  8. return handler ACK only for confirmed success; otherwise return NACK
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

The production updater now calls `mcu_update_run_image()` directly. The function
returns `0` only after `OTA_STATE_CONFIRMED`; a separate result field retains
the terminal lifecycle state for diagnostics. Package-policy, token-policy,
transport, execution, and internal failures use distinct non-zero codes.

## Boundaries retained

- SWUpdate remains the only HawkBit/DDI client and signed-SWU verifier.
- `mcu_updater.c` owns bounded Remote Handler reception into a transaction-local
  anonymous file; no package is published to persistent storage.
- `LSS_gateway` initializes the only LSS Master and official Gateway ASCII
  instances and connects the socket and CAN event loop following official
  `CO_main_basic.c` and `CO_epoll_interface.c`.
  Official `CO_fifo` and `CO_gateway_ascii` parse commands and run the state machine;
  the gateway receives no images and persists no identity mappings.
- `mcu_updater.c` extracts the image identity and sends `lss_allnodes 100 2 1`, `lss_switch_sel`,
  `lss_get_node`, and `lss_switch_glob 0` through one connection to
  `/run/LSS_gateway/command.sock`. It initializes no CANopen or LSS instance.
- `mcu_update_run_image()` accepts the active Node-ID as task input and joins
  the image, the direct TA client, and the OTA executor. It performs no LSS query.
- `ota_executor` remains the only OTA lifecycle state machine.
- The dedicated OP-TEE TA accepts only a SecurityAccess seed and constructs the
  complete protocol-bound token in Secure World.
- MCUboot remains the final image authenticity and rollback authority.

## Deliberate constraints

The package carries the target factory identity. The updater requests Fastscan
and queries the resident gateway for the active Node-ID in one exclusive session
before calling the OTA function. The
OTA task derives request/response IDs with
`CAN_ID_UDS_REQUEST(node_id)` and `CAN_ID_UDS_RESPONSE(node_id)`. Parallel
multi-node scheduling is deliberately absent. Starting allocation at `2` is valid
for the current single MCU; multi-MCU commissioning interrupted after partial
persistence requires a recoverable allocation strategy. Identity verification before and after reset uses the UDS
factory identity DID. No identity/Node-ID mapping is persisted. SWUpdate
`hardware-compatibility` is not used.
