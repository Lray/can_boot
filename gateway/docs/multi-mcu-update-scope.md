# Multi-MCU update scope

One Gateway controls multiple STM32 MCUs on one Classic CAN interface. Every MCU
has an immutable factory LSS identity and one commissioned CANopen Node-ID.
Updates are serialized by a per-interface lock; parallel programming is outside
the product scope.

## Commissioning

`gateway-lss-master` calls the official CANopenNode LSS Master API directly.
It selects or fast-scans one slave, assigns and stores a Node-ID, deselects it,
then reselects the identity and inquires the active Node-ID. Only that verified
assignment is persisted.

The registry is stored as a versioned, CRC-protected file under
`/var/lib/mcu-update/devices/<ifname>.bin`. Duplicate identities and Node-IDs,
corrupt files and conflicting assignments are rejected. Commissioning and
updating share `/run/mcu-update/<ifname>.lock`.

Restart `mcu-updater.service` after adding an MCU so it binds the new signed
identity endpoint.

## Address allocation

Node-IDs 1 through 127 are accepted. The official CANopen LSS COB-IDs remain
unchanged. Application traffic uses project allocations derived from the active
Node-ID:

| Function | 11-bit CAN ID |
| --- | --- |
| UDS request over ISO-TP | `0x600 + Node-ID` |
| UDS response over ISO-TP | `0x580 + Node-ID` |
| MCU log | `0x680 + Node-ID` |
| Heartbeat | `0x700 + Node-ID` |

The request/response ranges reuse otherwise disabled default SDO allocations;
this product does not enable SDO. They carry ISO-TP/UDS, not CANopen SDO.

An unconfigured MCU exposes LSS only. After its first stored assignment and LSS
deselect, firmware rebuilds all node-based filters and transmit buffers. A
configured Node-ID change becomes active only after the communication reset
required by the official LSS slave behavior.

## Update selection

The signed SWU description names one LSS identity. The Gateway resolves it to a
Node-ID from the registry, opens the corresponding ISO-TP channel, and verifies
the actual MCU identity over UDS before and after programming. Registry routing
alone is never sufficient to authorize a write.
