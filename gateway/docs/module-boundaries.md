# Gateway Module Boundaries

This workspace follows the architecture constraints in:

- `E:\T527\docs\2026-06-22-dual-thread-mcu-linux-workplan.md`

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

- compiler-safe memory clearing for secrets and monotonic timing primitives.

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

## UDS Client

Owner:

- `src/uds/uds_client.h`
- `src/uds/uds_client.c`
- `../third_party/iso14229/iso14229.c`
- `../third_party/iso14229/iso14229.h`

Responsibility:

- bind the unmodified upstream `UDSClient_t` to `TransportOps`
- expose the typed services required by diagnosis and OTA
- enforce the updater-wide deadline and product response payload checks

Rules:

- UDS client depends on transport callbacks, not on concrete SocketCAN or
  Linux ISO-TP socket details.
- P2/P2*, standard NRC, `0x78` and request state belong to upstream.
- OTA policy stays outside the vendored library and adapter.
- Vendored files are not the place for gateway retry, package, or
  progress behavior.

## Security Access

Current owner:

- `src/security/security_access.h`
- `src/security/security_access.c`

Responsibility:

- request the MCU seed through the UDS client
- pass only the seed challenge to the dedicated token TA
- send the resulting token and report the unlock result

Rules:

- SecurityAccess orchestration must use `UdsClient`; it must not open CAN or
  ISO-TP sockets directly.
- Token construction and private-key handling stay in the dedicated TA;
  `src/security/security_token_tee.*` owns only the direct libteec session.
- This layer owns gateway-side client sequencing, not MCU-side SecurityAccess
  state or cryptographic policy.

## Package Reading

Current package-boundary owner:

- `src/package/package_input.h`
- `src/package/package_metadata.h`
- `src/package/package_metadata.c`

Responsibility:

- package-input member and directory names shared by the updater and bridge
- descriptor-free package loading: read the signed image, derive size
  and declared MCUboot header version from `image.bin`; validate size
  boundaries needed before transfer

The gateway does not make the MCUboot security decision. Signature/TLV,
security-counter, slot selection, boot and rollback decisions remain with the
MCU's MCUboot handoff. A package-format check may reject malformed input, but
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

- `src/ota/transfer.h`
- `src/ota/transfer.c`
- `src/ota/ota_runtime.h`
- `src/ota/ota_snapshot.h`
- `src/ota/ota_snapshot.c`

`ota_executor` owns the complete OTA lifecycle and receives its UDS client,
reconnect callback, and SecurityAccess token issuer through `OtaExecutorConfig_t`;
`ota_snapshot` receives the client, reconnect callback, and deadlines as plain
parameters. The snapshot module retains stable MCU observation without
depending on `ota_executor.h`.

Responsibility:

- `ota_executor` owns the complete OTA lifecycle: session entry, OTA-entry
  authorization, download preparation routine, extended `RequestDownload`, transfer, pre-check,
  reset, reconnect, and final MCU-state classification.
- `ota_snapshot` owns stable MCU observation: the injectable monotonic
  clock and sleep, single DID snapshot reads, and the two-reads-equal
  polling policy used before download and after reset.
- `transfer` starts and polls the MCU download-preparation routine, validates the
  subsequent `RequestDownload` response, and drives the already-authorized
  `0x36/0x37` transfer, enforcing byte count, block sequence, and the terminal
  state of one transfer session.

`transfer` owns the full image transfer and does not duplicate the executor
lifecycle.

Rules:

- Package loading and local manifest/SHA validation happen once before
  `ota_executor`; transfer code must not repeat them.
- Session entry, authorization, pre-check, reset, reconnect, and
  post-reset state classification must not be exposed as a second flow.
- These modules depend on typed UDS operations and must not include SocketCAN
  or Linux ISO-TP headers.
- The retained host test is limited to sequential transfer behavior and typed
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
- It is not launched, managed, or read by the MCU updater. It is not an OTA
  acceptance gate.

## Applications and Adapters

Linux process entry points and external-system adapters are kept outside the
protocol and domain library:

- `apps/LSS_gateway/`: resident LSS Master, official Gateway ASCII and command socket.
- `apps/mcu_updater/`: production image receiver and update-job service.
- `apps/log_receiver/`: manually launched MCU ULog diagnostic receiver.
- `adapters/swupdate/`: production entry path
  `hawkBit -> SWUpdate Remote Handler -> ZeroMQ -> mcu-updater`.
  The project-local `remote_handler` adapter owns the ZeroMQ REP endpoint and
  two-frame transport; `mcu_updater.c` owns the bounded INIT/DATA transaction
  and composes it with the single `mcu_update_run_image()` application service.
- `src/security/security_token_tee.*`: direct libteec client for the dedicated
  SecurityAccess token TA; it verifies the provisioned public key and requests
  only the protocol-bound token command.

Applications and adapters may compose the lower layers, but the lower layers
must not depend on process-specific IPC, external services, or application
lifecycle details.

## Tool Boundary

Tools under `tools/` are typed transport/diagnostic probes. They must not
construct an OTA lifecycle or bypass `ota_executor`.

Current tools:

- `tools/smoke/raw_can_smoke.c`: RAW CAN send smoke.
- `tools/smoke/uds_smoke.c`: minimal UDS/DID smoke.

The MCU updater has no package descriptor; the signed MCUboot image is the only
package member, and the SecurityAccess token contract (seed-challenge
claims) lives in `shared/security_token_profile.h`.

## Production MCU Updater

LSS gateway owner:

- `apps/LSS_gateway/LSS_gateway.c` holds the only LSS Master and official Gateway
  ASCII instances and connects the CAN receive/timing loop to
  `/run/LSS_gateway/command.sock`, following official `CO_main_basic.c` and
  `CO_epoll_interface.c` organization.
- The daemon initializes the official modules and accepts one client. `CO_fifo`
  and `CO_gateway_ascii` own ASCII parsing and all command state machines;
  `CO_LSSmaster` owns the actual CAN LSS exchanges. No parser or Fastscan state
  machine is copied into the process entry point.
- The gateway does not initiate scanning at startup. Client disconnect deselects
  the LSS Master, restores its default timeout and clears the Gateway session.
- The daemon does not receive images, invoke OTA, or persist identity mappings.
- The only compiled sources are `LSS_gateway.c`, `CO_driver.c`, `CO_fifo.c`,
  `CO_LSSmaster.c` and `CO_gateway_ascii.c`. Upstream `cocomm` is the reference
  for client reception; its source is not vendored, compiled, deployed or invoked.

Owner:

- `apps/mcu_updater/mcu_updater.c`

Application-service owner:

- `apps/mcu_updater/update_job.h`
- `apps/mcu_updater/update_job.c`

Responsibility:

- `mcu_updater.c` receives one SWUpdate-verified image through the
  SWUpdate 2019.11 Remote Handler wire contract and buffers only the current
  transaction in an anonymous file.
- Product-local paths, `awlink0`, and the Remote Handler endpoint are constants;
  the production process has no generic runtime option parser.
- `mcu_updater.c` extracts the received image identity and sends the official
  `lss_allnodes 100 2 1`, selection, inquiry, and deselection commands over one exclusive gateway socket
  connection. It requires Node-ID `1..127` before calling the OTA function and
  initializes no CANopen or LSS instance.
- Client reception follows the official blocking-read example without an independent
  addressing deadline. `100` is the Fastscan step timeout in milliseconds.
- Starting allocation at Node-ID `2` applies to the current single MCU. Multi-MCU
  production requires a recoverable allocation strategy before this flow is extended.
- `mcu_update_run_image()` validates the received image FD and accepts the
  active Node-ID as input, configures that node's ISO-TP channel and
  direct TA client, and invokes the only OTA lifecycle in `ota_executor`.
- OTA verifies the package's factory identity through UDS before and after
  reset; package and OTA code do not depend on CANopen LSS types or APIs.
- The production process does not fork or execute a second OTA updater.
- The updater must not implement DDI, HTTP, SWU parsing/signature checking, or
  MCUboot boot policy; those belong to SWUpdate and MCUboot.

Rules:

- `remote_handler` owns the endpoint lock, socket ownership/mode checks, and
  two-frame ZeroMQ transport. It contains no package or updater policy.
- `mcu_updater.c` owns INIT/DATA sequencing, the announced-size bound, and the
  transaction-local `memfd`. SWUpdate still verifies the signed SWU and MCUboot
  remains the final MCU authority.
- Lower layers must not depend on SWUpdate adapter headers.
- `mcu-updater` returns ACK only after the selected MCU is running the confirmed
  image; all receive, protocol and MCU execution failures return NACK.
