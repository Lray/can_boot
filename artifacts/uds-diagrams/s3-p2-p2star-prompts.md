# S3 and P2/P2* sequence diagrams — ImageGen prompts

Generation mode: Codex built-in `image_gen` (`infographic-diagram`).

Code sources used to correct the supplied timeline:

- `shared/uds_protocol.h`
- `gateway/src/profile.h`
- `gateway/src/uds/uds_client.c`
- `gateway/src/uds/uds_transaction.c`
- `gateway/src/ota/ota_executor.c`
- `gateway/src/ota/transfer.c`
- `can/uds/uds_server.c`
- `gateway/docs/uds-mcu-alignment.md`

The current OTA erase flow does not use NRC `0x78`: `31 01 FF 00` starts the
asynchronous preparation; `31 03 FF 00` polls it; an unfinished poll returns
terminal NRC `7F 31 24`; the caller sleeps 250 ms and starts a new transaction.

## Sheet 1 — S3 keepalive

```text
Use case: infographic-diagram
Asset type: project technical sequence-diagram poster, sheet 1 of 2
Primary request: Create a precise 16:9 landscape sequence diagram dedicated to S3 session keepalive for the current E:\T527\can_boot implementation. Time flows top-to-bottom.
Actors: Gateway (Linux UDS Client); SocketCAN / ISO-TP / Classic CAN; MCU (UDS Server); S3Server timing lane.
Title: "S3 会话保活时序"
Subtitle: "Gateway (Linux UDS Client) ↔ MCU (UDS Server) · 当前代码实现"
Sequence:
1. `10 03` → MCU sets `S3 = now + 5100 ms` → `50 03 00 32 01 F4`.
2. `10 02` → MCU reloads `S3 = now + 5100 ms` → `50 02 00 32 01 F4`.
3. `3E 00` → MCU reloads S3 → `7E 00`.
4. `27 01 / 27 02 <token>` → `67 01 <seed> / 67 02`; ordinary business requests do not reload S3.
5. Erase-preparation loop: `31 01 FF 00` → `71 01 FF 00`; poll `31 03 FF 00` → while pending `7F 31 24`; every 1 s `3E 00 ↔ 7E 00`.
6. Transfer loop: `36 BSC <data>` → `76 BSC`; immediately after every successful block `3E 00 ↔ 7E 00`.
7. Failure branch: no valid keepalive for 5100 ms → `Δt ≥ 5100 ms` → default session → `SecurityAccess_ClearUnlock()`; next `36 BSC <data>` → `7F 36 22`.
Rules: `S3Server = 5100 ms`; only non-default sessions time out; `10 02 / 10 03` and valid `3E 00 / 3E 80` reload S3; `3E 80` suppresses the positive response; `0x27 / 0x31 / 0x34 / 0x36 / 0x37` do not reload S3; timeout returns to default session and clears security unlock.
Implementation note: Gateway keepalive failure does not immediately abort; a later business request exposes the lost session via NRC.
Footer: `Classic CAN · 请求 ID 0x7E0 → MCU · 响应 ID 0x7E8 → Gateway · 500 kbit/s`.
Style: clean white vector-like engineering infographic; Gateway blue, transport cyan, MCU teal, S3 purple, failures red; exact legible Chinese and protocol bytes; no watermark, logos, fictional services, tiny text, or ResponsePending content.
```

## Sheet 2 — P2/P2* response timing

```text
Use case: infographic-diagram
Asset type: project technical sequence-diagram poster, sheet 2 of 2
Primary request: Create a precise 16:9 landscape sequence diagram dedicated to P2/P2* response timing for the current E:\T527\can_boot implementation. Time flows top-to-bottom.
Actors: Gateway (Linux UDS Client); SocketCAN / ISO-TP / Classic CAN; MCU (UDS Server); receive-window timing lane.
Title: "P2 / P2* 响应超时时序"
Subtitle: "首响应先等 P2；仅有效 0x78 让下一次等待切换到 P2*"
Negotiation: `10 02` → `50 02 00 32 01 F4`; `P2ServerMax = 50 ms`; `P2*ServerMax = 5000 ms`; wire `P2 = 00 32`, `P2* = 01 F4 × 10 ms`; Gateway adopts valid 0x50 timing values.
Panel A — fast response: `22 F1 80`; wait P2=50 ms; MCU responds `62 F1 80 <data>` after about 5 ms; `5 ms < 50 ms`.
Panel B — generic ResponsePending: send `SID <request>`; first wait uses P2; before expiry MCU sends exact three-byte, matching-SID `7F <SID> 78`; Gateway increments `pending_count` and the next wait uses P2*=5000 ms. Repeated valid 0x78 causes another P2* wait. Finish with matching positive `<SID+0x40> <response>` or terminal `7F <SID> <终态 NRC>`.
Guardrails: accept at most 8 NRC 0x78; the ninth returns `UDS_ERR_RESPONSE_PENDING_LIMIT`; total transaction budget is 30000 ms; each recv is clamped to min(P2 or P2*, remaining transaction budget, operation deadline); a multi-frame request adds `CF count × STmin + 3000 ms` to the first wait.
Panel C — current OTA erase does not use 0x78: `31 01 FF 00` → under P2 `71 01 FF 00`; caller polls `31 03 FF 00`; while unfinished MCU returns `7F 31 24`; explain that 0x24 is terminal for that transaction and does not switch to P2*; caller sleeps 250 ms and starts a new transaction; every 1 s `3E 00 ↔ 7E 00`; completion is `71 03 FF 00 00 00 00 00`.
Comparison: P2 is the first-response deadline; P2* is the next-response deadline after valid `7F <SID> 78`; S3 is an independent session-liveness deadline.
Footer: `Classic CAN · 请求 ID 0x7E0 → MCU · 响应 ID 0x7E8 → Gateway · 500 kbit/s`.
Style: same visual family as sheet 1; P2/P2* orange, success green, current-OTA exception purple, errors red; exact legible Chinese and protocol bytes; no watermark, logos, invented bytes, tiny text, or claim that current OTA erase sends 0x78.
```
