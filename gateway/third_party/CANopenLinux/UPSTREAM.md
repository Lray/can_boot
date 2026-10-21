# CANopenLinux source provenance

- Upstream: <https://github.com/CANopenNode/CANopenLinux>
- Source commit: `f1348d4072cdabea4c3435a13c721ac29ab4cc91`
- Retrieved: 2026-09-16
- License: Apache License 2.0 (`LICENSE`)
- Project CANopenNode commit: `9b8beed8367241e96ac03f916cd5a500bcb2cf23`

The updater's fixed-command response reader follows upstream
[`cocomm.c` / `printReply`](https://github.com/CANopenNode/CANopenLinux/blob/f1348d4072cdabea4c3435a13c721ac29ab4cc91/cocomm/cocomm.c):
blocking reads, CRLF response completion and a 100-byte retained response tail.
The reference client is not vendored, compiled, deployed or invoked.

`apps/LSS_gateway/LSS_gateway.c` follows the single-thread mainline organization
of upstream `CO_main_basic.c` and the local socket integration in
`CO_epoll_interface.c`: create epoll and timerfd, create the command socket,
initialize CAN/LSS/GTWA, enable CAN, then wait, dispatch CAN and socket input,
process the official engine and adjust the next timer. The mainline interval
is the upstream `100000` microseconds; socket timeout is disabled as in the
upstream default. Only `awlink0` and the product Unix socket are supported.
Neither upstream application file is compiled, and no `CO_t` or OD is created.
On disconnect the product deselects LSS, restores the default LSS timeout, and
reinitializes GTWA for the next exclusive session.

`CO_driver.c`, `CO_driver_target.h`, `CO_error.h` and `CO_error_msgs.h` are
byte-identical to the source commit above. Project configuration lives in
`src/CO_driver_conf.h`, preincluded with the compiler's `-include` option so
the upstream header preserves its guarded defaults without modification.
It enables only the LSS Master, LSS ASCII commands and ASCII FIFO;
the command buffer uses the upstream 2000-byte default. The driver uses
`CO_SINGLE_THREAD`, `CO_DRIVER_MULTI_INTERFACE=0` and
`CO_DRIVER_ERROR_REPORTING=0`. `LSS_gateway.c` supplies the application
`log_printf` callback using the upstream example's `vsyslog` implementation.
No error-handler source, storage module or additional CANopen service is
compiled. The driver is compiled against the project's fixed CANopenNode
sources; the CANopenLinux CANopenNode submodule is not vendored.
