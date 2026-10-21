# CANopenNode provenance

- Upstream: `https://github.com/CANopenNode/CANopenNode.git`
- Source commit: `9b8beed8367241e96ac03f916cd5a500bcb2cf23`
- Retrieved: 2026-09-15
- License: Apache-2.0 (`CANopenNode/LICENSE` in the complete upstream tree)

The LSS, FIFO and ASCII gateway sources and their required headers are byte-identical
to that commit. The MCU compiles the unchanged LSS Slave directly against the official
`301/CO_driver.h`; target-specific STM32U5/FDCAN definitions and implementation
live in `can/transport/CO_driver_target.h` and `CO_driver_STM32.c`. It does not
compile a standalone CANopenNode stack.

The Linux `LSS_gateway` compiles only `301/CO_fifo.c`, `305/CO_LSSmaster.c`, and
`309/CO_gateway_ascii.c`, with the project SocketCAN driver and minimal mainline.
ASCII parsing and the LSS command state machines remain unchanged upstream code.
The SDO, NMT, Emergency, LED, and OD interface headers are retained solely because
the upstream gateway headers include them; their source files are not vendored
or compiled for this gateway. `LICENSE` is the upstream Apache-2.0 license.
Do not patch vendored sources.

The existing `301/crc16-ccitt.c` and header are adapted copies from the same
upstream; neither is compiled by `LSS_gateway`.
