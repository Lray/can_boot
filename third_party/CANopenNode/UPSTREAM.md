# CANopenNode provenance

- Upstream: `https://github.com/CANopenNode/CANopenNode.git`
- Source commit: `9b8beed8367241e96ac03f916cd5a500bcb2cf23`
- Retrieved: 2026-09-15
- License: Apache-2.0 (`CANopenNode/LICENSE` in the complete upstream tree)

The files currently present below `305/` and `storage/` are byte-identical to
that commit. The MCU compiles the unchanged LSS Slave through its target driver
ABI adapter; it does not compile a standalone CANopenNode stack. Generic
storage and LSS Master remain provenance sources until their Linux integration
is implemented. Do not patch the vendored protocol or generic storage sources.
