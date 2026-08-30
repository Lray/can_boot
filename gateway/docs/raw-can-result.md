# RAW CAN Result

Status: `RAW CAN 双端通信通过`

Date: 2026-06-23

Observed interface:

- Linux CAN network interface: `awlink0`
- Device-tree node: `awlink0`
- Bitrate: `500000`
- Sample point: `0.875`
- CAN state: `ERROR-ACTIVE`

Observed ECU traffic:

- `candump -tz awlink0` continuously receives ECU heartbeat standard frame `0x700`.
- Heartbeat payload starts with `A5`.

Observed request/response:

- Gateway request: `0x7E0#1122334455667788`
- ECU response: `0x7E8#1122334455667788`

Observed link counters:

- `berr-counter tx 0 rx 0`
- `bus-errors 0`
- `error-warn 0`
- `error-pass 0`
- `bus-off 0`
- RX/TX errors: `0`

Conclusion:

- RAW CAN two-end communication is complete.
- Seeing only the local `0x7E0` request is not considered success.
- The next validation is `ISO-TP`: single-frame echo, multi-frame echo, flow-control profile `BS=8` and `STmin=2 ms`, then timeout handling.
