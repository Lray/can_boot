# CANopenLinux source provenance

- Upstream: <https://github.com/CANopenNode/CANopenLinux>
- Source commit: `f1348d4072cdabea4c3435a13c721ac29ab4cc91`
- Retrieved: 2026-09-16
- License: Apache License 2.0
- Project CANopenNode commit: `9b8beed8367241e96ac03f916cd5a500bcb2cf23`

`CO_driver.c` and `CO_driver_target.h` are adapted from the upstream Linux
SocketCAN driver. The adaptation removes multi-interface support, CAN error
reporting, logging integration, timestamps, pthread locks, and all default
CANopen services other than LSS Master. It retains the upstream direct
SocketCAN and epoll architecture and is compiled against the project's fixed
CANopenNode sources; the CANopenLinux CANopenNode submodule is not vendored.

`CO_storageLinux.c` and `CO_storageLinux.h` retain the upstream file-plus-CRC
format and public naming. Their API and entry management are intentionally
adapted into standalone filesystem persistence: Object Dictionary entries,
OD 0x1010/0x1011, SDO, `CO_storage.c`, and CAN module arguments are removed.
Writes use a temporary file followed by rename so an interrupted update does
not replace the last complete file.

The CRC implementation in `third_party/CANopenNode/301/crc16-ccitt.c` and its
header come from the fixed CANopenNode commit above, not from CANopenLinux's
different submodule revision.
