# CANopenNode provenance

- Upstream: `https://github.com/CANopenNode/CANopenNode.git`
- Source commit: `9b8beed8367241e96ac03f916cd5a500bcb2cf23`
- Retrieved: 2026-09-15
- License: Apache-2.0 (`CANopenNode/LICENSE` in the complete upstream tree)

The files currently present below `301/` and `305/` are byte-identical to that
commit. The MCU compiles the unchanged LSS Slave directly against the official
`301/CO_driver.h`; target-specific STM32U5/FDCAN definitions and implementation
live in `can/transport/CO_driver_target.h` and `CO_driver_STM32.c`. It does not
compile a standalone CANopenNode stack. LSS Master remains a provenance source
until its Linux integration is implemented. Do not patch vendored sources.
