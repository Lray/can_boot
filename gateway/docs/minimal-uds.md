# Minimal UDS

Scope:

- `0x10 DiagnosticSessionControl`
- `0x3E TesterPresent`
- `0x22 ReadDataByIdentifier`

Out of scope:

- OTA routine/download/transfer/reset services
- package parsing
- transfer scheduler
- flash, update-state, or boot handoff

Module boundary:

- `src/uds/uds_client.*` implements minimal UDS request/response handling.
- `src/transport/transport.h` defines the narrow send/receive callback boundary.
- `src/transport/isotp_channel.*` is the Linux ISO-TP adapter and owns socket details.
- `tools/smoke/uds_smoke.c` wires the live ISO-TP channel to the UDS client for board validation.
- Host tests use a fake transport and do not need CAN hardware.

Host validation:

- `tests/test_uds_client.c` verifies:
  - programming session request/positive response
  - tester present request/positive response
  - DID read payload extraction
  - negative response reporting
  - `0x10` 完整解析并保存 ECU 公布的 P2/P2*（P2* wire unit 为 10 ms）
  - 仅收到同 SID、长度正确的 `0x7F <SID> 0x78` 后，才从协商 P2 切换到协商 P2*；最多接受 ECU profile 规定的 8 个 pending

Board validation command:

```bash
cd /opt/can-ota-gateway
./scripts/verify_uds.sh awlink0 .
```

Required board evidence:

- `DiagnosticSessionControl 0x03 PASS`
- `DiagnosticSessionControl 0x02 PASS`
- `TesterPresent PASS`
- `ReadDID 0xF180 PASS`
- `ReadDID 0xF181 PASS`
- `ReadDID 0xF182 PASS`
- `ReadDID 0xF1A0 PASS`
- `ReadDID 0xF1A6 PASS`
- `ReadDID 0xF1A8 PASS`
- one `NegativeResponse PASS` line
- final `Minimal UDS PASS`
- link remains `ERROR-ACTIVE` with stable error counters
