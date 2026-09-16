# ULog raw-CAN protocol

The MCU's ULog backend sends live text logs to the Gateway on standard CAN ID
`0x680 + active Node-ID`. This is a proprietary MCU-to-Gateway broadcast and
is not an ISO-TP or UDS message.

On the MCU, RT-Thread keeps the ULog asynchronous formatter, the raw-CAN log
transmitter, and the update/UDS thread as separate execution contexts.  The log
transmitter only drains the bounded ULog queue; it never enters ISO-TP, UDS, or
download code.  CAN transmission is arbitrated in the driver so a log frame
cannot corrupt an OTA response.

`Core/Src/fdcan.c` remains the CubeMX-generated initialization/MSP boundary.
Runtime queues, HAL callbacks, and BUS-OFF handling live in
`transport/CO_driver_STM32.c`; `fdcan.h` exposes the single HAL-handle accessor
while keeping the handle owned by the generated file.
The periodic `0x700 + active Node-ID` heartbeat is emitted by its own RT-Thread task,
separate from both the update/UDS thread and the ULog CAN transmitter.  It is a
project-specific, CANopen-inspired liveness frame with DLC `1` and state byte
`0x05`; it is not a transport-statistics channel or a complete CANopen NMT
Heartbeat implementation.

Each Classic CAN frame carries the following byte layout:

| Byte | Meaning |
| --- | --- |
| 0 | bit 7: start; bit 6: end; bits 5..0: zero-based fragment index |
| 1..7 | one to seven bytes of the formatted ULog text |

The final frame's DLC is `1 + final payload length`; records are limited to 64
frames (448 bytes).  The current ULog line buffer is 128 bytes, so it always
fits.  An MCU transmit-FIFO rejection ends the current record immediately; the
backend does not wait, retry, or enter the UDS transport path.  The Gateway
receiver discards an incomplete record whenever a fragment is missing,
out-of-order, oversized, or superseded by a new start frame.

Run the independent Gateway receiver as:

```sh
gateway-log-receiver <can-interface> <node-id>
```

It opens a second SocketCAN raw socket filtered only to that node's log ID, so an OTA
thread can continue to own its ISO-TP socket without parsing or back-pressuring
live MCU logs.
