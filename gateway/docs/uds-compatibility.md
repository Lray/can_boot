# UDS compatibility facts

Status: Minimal UDS acceptance is pending review and must not be marked PASS.

This note records compatibility facts only. It does not assign final root cause.

Observed facts:

- ECU has been flashed with and is running the UDS server under review.
- Without ISO-TP padding:
  - command: `printf '10 03\n' | isotpsend -s 7E0 -d 7E8 -b awlink0`
  - observed TX: `7E0 [3] 02 10 03`
  - observed RX: `7E8 [3] 02 10 03`
  - interpretation for review: this is ECU raw echo fallback, not a UDS positive response.
- With ISO-TP padding:
  - command: `printf '10 03\n' | isotpsend -s 7E0 -d 7E8 -p 00:00 -b awlink0`
  - observed TX: `7E0 [8] 02 10 03 00 00 00 00 00`
  - observed RX: `7E8 [8] 06 50 03 00 32 13 88 00`
- The trace above is historical evidence, not a timing-profile acceptance value. Under
  ISO 14229 the last two bytes use a 10 ms unit, so `13 88` means 50000 ms.
  The current ECU/gateway profile requires `01 F4` for `P2*=5000 ms`; a new board
  capture must show that value before this validation is marked PASS.
- Correct receive direction for the response is `isotprecv -s 7E0 -d 7E8 ...`.
  Using `-s 7E8 -d 7E0` reads the local request direction and can mislead validation.
- `src/transport/isotp_channel.c` configures `CAN_ISOTP_RECV_FC` only and does not enable
  `CAN_ISOTP_TX_PADDING`.
- Linux SocketCAN/can-utils default short-DLC ISO-TP single-frame requests are legal.
  The ECU should accept both legal short-DLC single frames and padded single frames.
- TX padding is a temporary compatibility/debug option for retest, not the only correct
  diagnostic path.

Current action:

- Keep gateway code unchanged.
- Do not enter package parsing, transfer scheduler, OTA SIDs, flash/update-state, or boot handoff.
- Wait for ECU-side fix/retest of short-DLC ISO-TP receive and raw echo fallback behavior.
