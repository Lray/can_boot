# ISO-TP Result

Status: `ISO-TP 验收已通过`

Date: 2026-06-23

Observed platform:

- Kernel has `CONFIG_CAN_ISOTP=y`.
- Kernel log includes `can: isotp protocol`.
- Interface: `awlink0`.
- Bitrate: `500000`.
- Final CAN state: `ERROR-ACTIVE`.
- Final `berr-counter tx 0 rx 0`.

Observed ISO-TP profile:

- Request ID: `0x7E0`.
- Response ID: `0x7E8`.
- Single-frame echo PASS: `11 22 33 44 55 66 77`.
- Multi-frame echo PASS: `11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF 00 11 22 33 44 55 66 77 88`.
- Flow Control observed: `30 08 02 00 00 00 00 00`.
- Flow Control meaning: CTS, `BS=8`, `STmin=2 ms`.

Conclusion:

- ISO-TP transport is ready for minimal UDS.
- The next validation is limited to `0x10`, `0x3E`, and `0x22`.
- OTA services remain out of scope until the diagnostic contract passes.
