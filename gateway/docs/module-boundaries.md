# Gateway Module Boundaries

This workspace follows the architecture constraints in:

- `E:\T527\docs\2026-06-22-dual-thread-ecu-linux-workplan.md`

The gateway code must stay high-cohesion and low-coupling. Do not merge CAN,
ISO-TP, UDS, package parsing, transfer scheduling, and reporting into one large
implementation file.

## Shared Constants

Owner:

- `src/profile.h`

Responsibility:

- CAN IDs and bitrate
- ISO-TP defaults
- UDS timing
- UDS routine IDs
- transfer block sizes
- diagnostic DID IDs

Rules:

- Other modules read shared values from this header.
- Do not copy these constants into unrelated `.c` files.
- Shared protocol changes must be made in `E:\T527\docs` first.

## Byte-order Codec

Owner:

- `src/codec/byte_order.h`
- `src/codec/byte_order.c`

Responsibility:

- Encode and decode fixed-width unsigned integers in protocol byte order.

Rules:

- Protocol modules use this API instead of defining local byte-order helpers.
- This module contains no UDS, OTA, transport, or package policy.

## Shared Utility Primitives

Owner:

- `src/util/util.h`
- `src/util/util.c`

Responsibility:

- `secure_zero`: compiler-safe memory clearing for secrets.
- `is_lower_hex`, `is_uuid_v4`, `valid_ifname`: input validators shared by
  package, worker, and bridge code.

Rules:

- This module is pure C with zero project dependencies.
- Other modules use these functions instead of defining local copies.

## SocketCAN Device Access

Owner:

- `src/transport/socketcan_raw.h`
- `src/transport/socketcan_raw.c`

Responsibility:

- Linux RAW SocketCAN open/bind/send/receive
- explicit classic CAN filter installation

Rules:

- This layer may include Linux SocketCAN headers.
- ISO-TP, UDS, package, and scheduler modules must not depend on RAW CAN frame
  structures unless they are explicitly implementing a RAW CAN tool.
- Hardware-specific device access stays here or in a future dedicated platform
  adapter file.

Current command-line user:

- `tools/smoke/raw_can_smoke.c` owns its own hex argument parsing and supplies
  explicit filters to this transport.

## ISO-TP Transport

Current owner:

- `src/transport/isotp_channel.h`
- `src/transport/isotp_channel.c`
Current validation script:

- `scripts/verify_isotp.sh`

Responsibility:

- Linux ISO-TP socket lifecycle
- request/response CAN ID binding
- BS/STmin option setup
- transport send/receive primitives for UDS

Rules:

- UDS client code must call ISO-TP through a narrow transport interface.
- UDS code must not open Linux sockets directly.
- SocketCAN details stay below this layer.

## UDS Transaction Engine

Owner:

- `src/uds/uds_transaction.h`
- `src/uds/uds_transaction.c`

Responsibility:

- one request/response transaction over injected transport callbacks
- P2/P2* deadlines, `0x78 ResponsePending`, NRC framing, response capture, and
  the transaction-wide deadline

Rules:

- This module owns generic transaction mechanics only.
- Session semantics, DID semantics, and OTA policy stay in their callers.
- It must not depend on concrete SocketCAN or Linux ISO-TP implementations.

## UDS Client

Future owner:

- `src/diag_client.h`
- `src/diag_client.c`
- `third_party/iso14229/`
Current minimal UDS owner:

- `src/uds/uds_client.h`
- `src/uds/uds_client.c`

Responsibility:

- trimmed `driftregion/iso14229` client integration
- `0x10`, `0x3E`, `0x22`, then OTA services after minimal UDS
- NRC and `0x78 ResponsePending` handling

Rules:

- UDS client depends on transport callbacks, not on concrete SocketCAN or
  Linux ISO-TP socket details.
- OTA policy stays outside the vendored library.
- Vendored files are not the place for gateway retry, package, resume, or
  progress behavior.

## Security Access

Current owner:

- `src/security/security_access.h`
- `src/security/security_access.c`

Responsibility:

- request the ECU seed through the UDS client
- pass only the seed challenge to the signer client
- send the resulting token and report the unlock result

Rules:

- SecurityAccess orchestration must use `UdsClient`; it must not open CAN or
  ISO-TP sockets directly.
- Token signing and private-key handling stay behind the signer client
  boundary in `src/security/token_signer_client.*` (client) and
  `apps/token-signer-daemon/` (TEE-backed signer).
- This layer owns gateway-side client sequencing, not ECU-side SecurityAccess
  state or cryptographic policy.

## Package Reading

Current package-boundary owner:

- `src/package/package_input.h`
- `src/package/package_metadata.h`
- `src/package/package_metadata.c`
- `src/package/sha256.h`
- `src/package/sha256.c`

Responsibility:

- package-input member and directory names shared by the worker and bridge
- descriptor-free package loading: read the signed image, derive size, digest,
  and declared MCUboot header version from `image.bin`; validate size
  boundaries needed before transfer

The gateway does not make the MCUboot security decision. Signature/TLV,
security-counter, slot selection, boot and rollback decisions remain with the
ECU's MCUboot handoff. A package-format check may reject malformed input, but
it must not be reported as final authenticity or boot verification.

Rules:

- Package code must not open CAN, ISO-TP, or UDS sessions.
- `package_metadata` reads package files and returns validated data to the
  scheduler; it does not own transport I/O.

## OTA Execution

The only OTA owner is:

- `src/ota/ota_executor.h`
- `src/ota/ota_executor.c`

Its private transfer and observation collaborators are:

- `src/ota/resume_transfer.h`
- `src/ota/resume_transfer.c`
- `src/ota/ota_runtime.h`
- `src/ota/ota_snapshot.h`
- `src/ota/ota_snapshot.c`

`ota_executor` owns the complete OTA lifecycle and receives its UDS client,
reconnect callback, and signer directly through `OtaExecutorConfig_t`;
`ota_snapshot` receives the client, reconnect callback, and deadlines as plain
parameters. The snapshot module retains stable ECU observation without
depending on `ota_executor.h`.

Responsibility:

- `ota_executor` owns the complete OTA lifecycle: session entry, OTA-entry
  authorization, download preparation routine, extended `RequestDownload`, transfer, pre-check,
  reset, reconnect, and final ECU-state classification.
- `ota_snapshot` owns stable ECU observation: the injectable monotonic
  clock and sleep, single DID snapshot reads, and the two-reads-equal
  polling policy used before download and after reset.
- `resume_transfer` starts and polls the ECU download-preparation routine, validates the
  subsequent `RequestDownload` response, calculates the remaining transfer from its durable offset, and drives the
  already-authorized `0x36/0x37` transfer, enforcing byte count, block
  sequence, and the terminal state of one transfer session.

`resume_transfer` owns resume arithmetic and does not duplicate the executor
lifecycle.

Rules:

- Package loading and local manifest/SHA validation happen once before
  `ota_executor`; transfer code must not repeat them.
- Session entry, authorization, pre-check, reset, reconnect, and
  post-reset state classification must not be exposed as a second flow.
- These modules depend on typed UDS operations and must not include SocketCAN
  or Linux ISO-TP headers.
- The retained host test is limited to pure resume-plan arithmetic and typed
  UDS/transfer codec boundaries. Lifecycle acceptance is board/HIL evidence.

## Diagnostic ULog Receiver

Current owners:

- `src/log/log_frame.h`
- `src/log/log_frame.c`
- `src/log/log_receiver.h`
- `src/log/log_receiver.c`
- `src/log/log_receiver_stats.h`
- `src/log/log_sink.h`
- `src/log/log_sink.c`

Responsibility:

- `log_frame`: MCU ULog raw-CAN frame constants, validation, and decoding.
- `log_receiver`: fragment ordering and complete-record reassembly only.
- `log_receiver_stats`: application-owned counters for completed records,
  abandoned records, and rejected frames.
- `log_sink`: output of complete records to an already opened stream. File
  creation and rotation are intentionally not implemented yet.

Rules:

- The frame decoder, receiver, and sink do not call UDS, classify OTA failures,
  or publish artifacts.
- The receiver does not own callbacks, output streams, or statistics.
- It is not launched, managed, or read by the OTA worker. It is not an OTA
  acceptance gate.

## Applications and Adapters

Linux process entry points and external-system adapters are kept outside the
protocol and domain library:

- `apps/ota_worker/`: OTA worker and package probe entry points.
- `apps/log_receiver/`: manually launched ECU ULog diagnostic receiver.
- `apps/token-signer-daemon/`: TEE-backed COSE token signer daemon; owns
  OP-TEE session integration and socket-side peer policy.
- `adapters/swupdate/`: single-process OTA orchestrator chain
  `hawkBit -> SWUpdate Remote Handler -> ZeroMQ -> worker`. `orchestrator`
  owns process composition; `wifi_ctrl` wraps the official `wpa_cli`
  commands for scanning and saved-network connect; `remote_handler` owns the
  ZeroMQ REP endpoint and frame protocol; `package_store` owns package
  receive, release-bound size/SHA-256 verification, and atomic publication of
  the worker input directory; `fs_util` owns filesystem primitives.
- `src/security/token_signer_*`: token signer client library; `token_signer_codec`
  owns CBOR and protocol framing while the client owns Unix
  socket policy and I/O. `token_signer_protocol.h` is the single source for the
  signer profile, response statuses, and message-size limits.

Applications and adapters may compose the lower layers, but the lower layers
must not depend on process-specific IPC, external services, or application
lifecycle details.

## Tool Boundary

Tools under `tools/` are typed transport/diagnostic probes. They must not
construct an OTA lifecycle or bypass `ota_executor`.

Current tools:

- `tools/smoke/raw_can_smoke.c`: RAW CAN send smoke.
- `tools/smoke/uds_smoke.c`: minimal UDS/DID smoke.

The OTA worker has no package descriptor; the signed MCUboot image is the only
package member, and the SecurityAccess token contract (seed-challenge
claims) lives in `shared/security_token_profile.h`. The worker CLI is isolated
in `ota_worker_args.c/.h`.

## OTA Orchestrator

Owner:

- `adapters/swupdate/orchestrator.c`
- `adapters/swupdate/orchestrator_args.h`
- `adapters/swupdate/orchestrator_args.c`

Responsibility:

- one-process composition of the delivery chain: bring up Wi-Fi via the
  official `wpa_supplicant`/`wpa_cli`, launch the SDK SWUpdate 2019.11
  Suricatta as a child, serve the official Remote Handler ZeroMQ endpoint,
  publish the verified package, and launch the OTA worker.
- The orchestrator must not implement DDI, HTTP, download, signing, or MCU
  transport; those belong to SWUpdate, the worker, and lower layers.

Rules:

- `wifi_ctrl` speaks only the official `wpa_cli` interface; no raw WPA
  protocol, no credential handling beyond reading the existing config file.
- `remote_handler` owns the endpoint lock, socket ownership/mode checks, and
  the two-frame INIT/DATA/ACK protocol. It contains no job, package, or worker
  policy.
- `package_store` owns receive state, the release-bound size/SHA-256 check,
  staged writes, and atomic rename. It contains no ZMQ or subprocess code;
  SWUpdate still verifies the signed SWU and MCUboot remains the final ECU
  authority.
- The orchestrator is the only caller of worker launch; workers never reach
  back into orchestrator state.
- Lower layers must not depend on orchestrator or adapter headers.
