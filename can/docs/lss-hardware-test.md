# MCU LSS hardware acceptance

Status: firmware and host tests pass; board evidence is pending hardware
connection.

## Preconditions

- Program a unique 16-byte Factory Identity at `0x081F4000` and verify its
  readback before starting the firmware. An erased identity is a fatal startup
  provisioning error.
- Erase `0x081F2000-0x081F3FFF` for the initial unconfigured test.
- Start the gateway CAN interface at 500 kbit/s and capture both `0x7E5` LSS
  Master requests and `0x7E4` LSS Slave responses with timestamps.
- Do not run an OTA transfer while performing the initial LSS acceptance test.

## Acceptance sequence

1. Cold boot with an erased communication page. Verify that LSS reports
   Node-ID `0xFF` and all four identity words match production data.
2. Select the MCU by its complete identity, then configure Node-ID `0x2A` at
   the existing 500 kbit/s. Send Store Configuration and require status `0`.
3. Read `0x081F2000` through the debugger. Its first little-endian word must be
   `0x0001F42A`; the remaining twelve bytes of the programmed quadword must be
   `0xFF`.
4. Power-cycle the MCU. Select it again and verify Node-ID `0x2A` is loaded.
5. Store the same configuration again. Require success and verify the page was
   not erased or programmed again.
6. Send an LSS Configure Bit Timing request for 250 kbit/s. The MCU must not
   acknowledge it, its CAN controller must remain at 500 kbit/s, and a
   subsequent Store Configuration must keep the word `0x0001F42A`.
7. Run one normal OTA and rollback cycle. Verify the identity page and
   communication word are unchanged afterward.

Measure the interval from each Store Configuration request to its response.
The largest observed page erase/program/readback time must remain below the
gateway LSS Master timeout. An interrupted communication-config write is
allowed to fall back to Node-ID `0xFF` and 500 kbit/s on the next cold boot.
