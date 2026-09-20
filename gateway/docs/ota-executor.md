# Canonical OTA execution

`ota_executor` is the Gateway's only OTA lifecycle owner. The production path
is:

```text
SWUpdate -> mcu-updater -> mcu_update_run_job -> ota_executor
         -> package load/structural validation -> ota_executor -> typed UDS
```

The updater loads and validates image metadata and length,
and image SHA once before the executor is called. This is a transfer-boundary
format/integrity check, not a signature, TLV, rollback, or boot-authenticity
decision. MCUboot on the MCU owns those final decisions.

`ota_executor` then owns, in one sequence: stable MCU observation; programming
session entry; SecurityAccess authorization for entering OTA;
the EraseMemory routine; standard `0x34` download request; `0x36/0x37` transfer; hard
reset; reconnect; and post-reset classification.

`transfer` is an internal transfer helper, not another OTA entry. It
assumes a loaded package and an already-open, authorized OTA session. It
starts the `0x31 FF00` EraseMemory routine and polls its results, then sends
`0x34` carrying only the standard address and size fields, and executes the
full transfer. The expected target slot for post-reset verification is derived
from the pre-update active slot; the MCU selects its inactive slot for writing.

`download` is likewise a transfer helper: it owns target selection, byte count,
block sequence, and terminal state for `0x31/0x34/0x36/0x37`. The typed UDS
client owns the routine's wire encoding. `0x31 FF00` (EraseMemory) owns Flash
erase; `0x34/0x36/0x37` do not erase Flash.

The post-reset `DID_APP_VERSION` observation means the version of the image
currently running on the MCU. Together with active slot and the startup
confirmation result, it classifies confirmed activation, rollback, failed
confirmation, or an indeterminate outcome. It is a state observation; it is
not a Gateway boot verifier.

There are no standalone download, transfer, or boot-handoff smoke executables.
RAW CAN and minimal UDS probes remain for their own transport/diagnostic
boundaries. OTA acceptance requires the real SWUpdate-to-updater path on a
board/HIL setup, including a valid package, SecurityAccess, reset/reconnect,
and observed post-reset MCU state.
