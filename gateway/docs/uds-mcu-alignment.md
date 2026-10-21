# Gateway / MCU UDS alignment

The T527 Gateway is the central controller. Each STM32 MCU is an LSS-addressed
node beneath it. LSS supplies the node identity and Node-ID; UDS then performs
diagnostics and firmware update against that selected node.

Both endpoints use the unmodified driftregion/iso14229 release 0.11.0
(commit 8a7eb23d6ab9d52f66ce36b07ba456613709c78e). The Gateway binds
UDSClient_t to Linux SocketCAN ISO-TP, and the MCU binds UDSServer_t to its
existing isotp-c channel. Product callbacks implement DID data,
SecurityAccess token verification, inactive-slot erase/write and reset.

## Transport and addressing

- Classic CAN, 11-bit normal addressing and 500 kbit/s.
- For Node-ID N, the Gateway sends at 0x600 + N and receives at 0x580 + N.
- ISO-TP uses a 512-byte UDS PDU limit, BS=8 and STmin=2 ms.
- SocketCAN delivers a complete ISO-TP PDU to the upstream UDS client.

## Standard timing and session behavior

- Initial and server timing is P2=50 ms, P2*=5000 ms; P2* is encoded in 10 ms
  units in a 0x10 response.
- The upstream client adopts valid P2/P2* values announced by the server.
- MCU S3Server is the upstream default 5100 ms. Upstream refreshes S3 for
  DiagnosticSessionControl and TesterPresent. Other services do not refresh it.
- The Gateway sends TesterPresent at 2000 ms intervals during long transfers.
- Upstream accepts successive valid 0x78 ResponsePending messages until a
  terminal response or a P2* / operation deadline. There is no project
  response-count limit.

## SecurityAccess

- The upstream server enforces its 1000 ms boot delay and 1000 ms delay after
  a failed key.
- A seed response after the level is already unlocked is the upstream
  two-byte zero seed. The Gateway treats any nonempty all-zero seed as already
  unlocked.
- A nonzero project seed is four bytes and is passed to the OP-TEE token
  provider. Token verification remains a product callback.

## Download behavior

The product sequence is:

1. 31 01 FF 00 starts inactive-slot erase.
2. 31 03 FF 00 polls the result; unavailable results return NRC 0x24, and a
   Flash failure returns 0x72.
3. 34 00 44 00 00 00 00 <size[4]> begins the transfer.
4. The server returns maxNumberOfBlockLength 258, allowing 256 payload bytes.
5. 0x36 transfers strict consecutive BSC values and 0x37 completes.

Per upstream behavior, a BSC mismatch returns NRC 0x24 and resets the upstream
transfer state. The Gateway does not retry a timed-out block. A new
erase/download sequence is required before continuing, which avoids writing a
block twice after an ambiguous timeout.

Standard NRC values are returned directly by the Gateway adapter. Product
code can inspect last_nrc for logging, but it does not translate a standard
NRC into a project-specific negative error.

Host coverage is in tests/test_uds_client.c, tests/test_transfer.c and
can/Tests/uds_server_host_test.c.
