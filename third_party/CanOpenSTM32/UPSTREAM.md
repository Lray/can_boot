# CanOpenSTM32 provenance

- Upstream: `https://github.com/CANopenNode/CanOpenSTM32.git`
- Source commit: `f2da6a65b84148670cf59eff7686cb7eb15ee0e9`
- CANopenNode submodule commit: `9b8beed8367241e96ac03f916cd5a500bcb2cf23`
- Retrieved: 2026-09-15
- License: Apache-2.0

`can/transport/CO_driver_target.h` and `CO_driver_STM32.c` are project
adaptations derived from the upstream STM32 driver at the commit above. They
retain the verified STM32U5/FDCAN behavior and expose the official CANopenNode
Driver ABI directly.

`CANopenNode_STM32/CO_storageBlank.c` and `.h` are project adaptations derived
from the upstream blank storage template. They intentionally replace the no-op
implementation with the STM32 internal-Flash Node-ID backend used by the
standalone LSS Slave. The standard `CO_storageBlank_init()` and
`CO_storageBlank_auto_process()` entry names are retained; unused Object
Dictionary parameters are omitted because this target does not link OD/SDO or
`CO_storage.c`. The CANopenNode LSS sources remain separately pinned and
unchanged.
