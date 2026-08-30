# ULog raw-CAN protocol

The ECU's ULog backend sends live text logs to the Gateway on standard CAN ID
`0x6D0`.  This is a proprietary ECU-to-Gateway broadcast and is not an ISO-TP
or UDS message: it never shares the `0x7E0` request or `0x7E8` response path.

On the ECU, RT-Thread keeps the ULog asynchronous formatter, the raw-CAN log
transmitter, and the OTA/UDS worker as separate execution contexts.  The log
transmitter only drains the bounded ULog queue; it never enters ISO-TP, UDS, or
download code.  CAN transmission is arbitrated in the driver so a log frame
cannot corrupt an OTA response.

`Core/Src/fdcan.c` remains the CubeMX-generated initialization/MSP boundary.
Runtime queues, HAL callbacks, counters, and BUS-OFF recovery live in
`transport/can_driver_stm32.c`; `fdcan.h` exposes the single HAL-handle accessor
while keeping the handle owned by the generated file.
The periodic `0x700` system heartbeat is emitted by its own RT-Thread worker,
separate from both the OTA/UDS worker and the ULog CAN transmitter.

Each Classic CAN frame carries the following byte layout:

| Byte | Meaning |
| --- | --- |
| 0 | bit 7: start; bit 6: end; bits 5..0: zero-based fragment index |
| 1..7 | one to seven bytes of the formatted ULog text |

The final frame's DLC is `1 + final payload length`; records are limited to 64
frames (448 bytes).  The current ULog line buffer is 128 bytes, so it always
fits.  An ECU transmit-FIFO rejection ends the current record immediately; the
backend does not wait, retry, or enter the UDS transport path.  The Gateway
receiver discards an incomplete record whenever a fragment is missing,
out-of-order, oversized, or superseded by a new start frame.

Run the independent Gateway receiver as:

```sh
gateway-ecu-ulog-receiver <can-interface>
```

It opens a second SocketCAN raw socket filtered only to `0x6D0`, so an OTA
worker can continue to own its ISO-TP socket without parsing or back-pressuring
live ECU logs.
