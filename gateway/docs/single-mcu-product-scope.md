# Single-MCU Secure Updater: Product Scope

## Product statement

This component securely and recoverably updates exactly one MCU attached to a
T527 Linux application processor. The MCU is resource-constrained and does not
need an IP stack or an OTA client. Linux owns fleet connectivity and package
delivery; the MCU owns final boot authenticity and rollback through MCUboot.

The supported production path is deliberately narrow:

```text
hawkBit
  -> official SWUpdate Suricatta and signed SWU verification
  -> mcu-updater
       receive bounded image stream
       enforce announced slot size and derive SHA-256
       atomically publish image.bin
       validate MCUboot image metadata
       use OP-TEE-backed UDS SecurityAccess
       transfer over fixed CAN / ISO-TP / UDS
       reconnect and confirm the running version
  -> MCUboot signature, security-counter, A/B boot and rollback decision
```

`mcu-updater` is the only production MCU-update process. It does not spawn
a second OTA updater. `mcu-updater-direct` is a separately deployed
diagnostic/production-line tool and calls the same `mcu_update_run_job()`
implementation; it is not part of the HawkBit runtime.

## Reliability and security invariants

- Success means the MCU rebooted, produced a stable post-reset snapshot, and
  reported the expected confirmed image. Receiving or transferring all bytes
  is not success.
- The updater returns process status `0` only for that confirmed terminal
  outcome. OTA lifecycle states are retained as diagnostic data and are never
  reused as process exit codes.
- SWUpdate verifies the signed description and complete artifact before the
  hardened Remote Handler forwards it. The receiver enforces the announced
  slot bound, derives SHA-256 from the received bytes, writes into an internal
  transaction directory, fsyncs, and atomically publishes the image.
- No release UUID, size, or digest is provisioned through systemd. The service
  remains resident across releases and creates a fresh internal transaction
  for every image. MCUboot remains the final authority for whether the MCU
  image may boot.
- UDS SecurityAccess limits who may enter the privileged flashing session. It
  is defense in depth and session authorization; it does not replace package
  or MCUboot signature verification.
- Transfer resume, UDS timeouts/NRC handling, reconnect, version observation,
  and MCUboot rollback stay in the single OTA state machine.

## Fixed product contract

- One MCU per Linux updater instance.
- One configured CAN interface.
- Physical request CAN ID `0x7E0` and response CAN ID `0x7E8`.
- One raw MCUboot `image.bin` per release.
- Serial update execution only.
- SWUpdate `hardware-compatibility` is not used.

The physical transport can be replaced by a UART implementation in a future
product variant, but transport replacement must remain below the UDS/update
state machine. It does not justify a multi-controller catalog or scheduler.

## Explicit non-goals

- Multi-MCU manifests, controller catalogs, node discovery, dynamic CAN IDs,
  or per-bus parallel scheduling.
- A complete CANopenNode or IP/OTA protocol stack on the MCU. The official
  standalone LSS Slave used for identity selection and communication
  configuration is the only CANopen exception.
- Uptane metadata roles or an automotive campaign-management platform.
- A second custom HawkBit DDI client, HTTP downloader, SWU parser, or MCUboot
  signature verifier.
- Treating the direct diagnostic executable as a second production path.

If a later product genuinely contains multiple independently updatable MCUs,
that is a new product requirement and should be designed as a separate branch,
not anticipated inside this single-MCU component.
