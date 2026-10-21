# MCU Secure Updater: Current Scope and Multi-MCU Boundary

## Product statement

This component securely and recoverably updates one selected MCU per
transaction on a T527 Linux gateway. A release artifact carries the target's
four-field factory identity. After receiving the image, `mcu-updater` requests
Fastscan/configuration, selection, Node-ID inquiry, and deselection through one
exclusive Unix socket connection to the resident `LSS_gateway`. The original
Gateway ASCII module parses commands and drives the original LSS Master.
The updater requires Node-ID `1..127` before calling the OTA
function, which derives its ISO-TP CAN identifiers before opening the UDS session.

The production path is:

```text
hawkBit
  -> SWUpdate Suricatta and signed SWU verification
  -> mcu-updater
       receive one bounded identity-envelope + MCUboot image
       send official ASCII commands to LSS_gateway over its command socket
         -> lss_allnodes 100 2 1, select the identity, query the active Node-ID, and deselect
       call the OTA function with that Node-ID as input
       update the selected MCU over SocketCAN / ISO-TP / UDS
       reconnect and confirm the selected MCU's running version
  -> MCUboot signature, security-counter, A/B boot and rollback decision
```

## Product contract

- The current product has one MCU; `lss_allnodes 100 2 1` starts allocation at `2`.
- The MCU has a factory LSS identity and stores its assigned Node-ID.
- Request, response, heartbeat, and log CAN IDs are derived from that Node-ID.
- One transaction targets one MCU and transactions execute serially.
- The package identity must match the MCU's UDS factory identity before and
  after reset; this verification belongs to OTA.
- The official gateway reports each commissioned identity/Node-ID pair but
  does not persist a routing registry. This project queries the active Node-ID
  before each update and does not persist a mapping.
- `LSS_gateway` is the sole owner of the LSS Master and Gateway ASCII instances
  and CAN receive/timing loop. It accepts commands only and scans when requested.
- `mcu-updater` owns image reception and OTA execution. It requests Node-ID
  over the gateway socket and initializes no CANopen or LSS instance.
- MCUboot remains the final image authenticity and rollback authority.

## Current validation boundary

Current board/HIL acceptance covers only one MCU. Multi-MCU productization must
first define recoverable Node-ID allocation. If commissioning loses power after
some nodes save their assignments, starting again at `2` can conflict with their
saved Node-IDs. This fixed-start flow must not be reused for that topology.

After allocation recovery is defined, multi-MCU HIL must verify at least two
distinct identities, distinct Node-IDs, correct target isolation, serial updates,
reset/reconnect, interrupted commissioning and failure containment.

## Non-goals

- Parallel updates on one or more CAN buses.
- A fleet campaign scheduler or controller catalog outside the signed release
  artifact's target identity.
- A second HawkBit/DDI client, HTTP downloader, SWU parser, MCUboot verifier,
  or MCU-update executable.
- A complete CANopen stack on the MCU beyond the official LSS behavior needed
  for identity selection and communication configuration.
