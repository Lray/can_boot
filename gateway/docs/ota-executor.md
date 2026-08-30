# Canonical OTA execution

`ota_executor` is the Gateway's only OTA lifecycle owner. The production path
is:

```text
SWUpdate -> ecu-ota-bridge -> gateway-ota-worker-v1
         -> package load/structural validation -> ota_executor -> typed UDS
```

The worker loads and validates package metadata, the manifest, image length,
and image SHA once before the executor is called. This is a transfer-boundary
format/integrity check, not a signature, TLV, rollback, or boot-authenticity
decision. MCUboot on the ECU owns those final decisions.

`ota_executor` then owns, in one sequence: stable ECU observation; extended
and programming session entry; SecurityAccess authorization for entering OTA;
download-preparation routine; extended `0x34` identity binding/resume decision; `0x36/0x37` transfer; hard
reset; reconnect; and post-reset classification.

`resume_transfer` is an internal transfer helper, not another OTA entry. It
assumes a loaded package and an already-open, authorized OTA session. It
passes the manifest's complete-image `image_sha256` unchanged in the extended
`0x31 F001` preparation request and result polling, then sends `0x34`, validates the returned target and durable cursor, and executes the
remaining transfer. It does not enter a session or recompute the payload hash.

`download` is likewise a transfer helper: it owns target selection, journal
matching, durable cursor handling, byte count, block sequence, and terminal
state for `0x31/0x34/0x36/0x37`. The typed UDS client owns the extension's wire
encoding. `0x31 F001` owns erase preparation; `0x34/0x36/0x37` do not erase Flash.

The post-reset `DID_APP_VERSION` observation means the version of the image
currently running on the ECU. Together with active slot and the startup
confirmation result, it classifies confirmed activation, rollback, failed
confirmation, or an indeterminate outcome. It is a state observation; it is
not a Gateway boot verifier.

There are no standalone download, resume, or boot-handoff smoke executables.
RAW CAN and minimal UDS probes remain for their own transport/diagnostic
boundaries. OTA acceptance requires the real SWUpdate-to-worker path on a
board/HIL setup, including a valid package, SecurityAccess, reset/reconnect,
and observed post-reset ECU state.
