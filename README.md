# CAN Boot and Gateway

This repository contains the two firmware/update components used by the project:

- `can/`: STM32 CAN/MCU firmware, boot and update support.
- `gateway/`: Linux SocketCAN/ISO-TP/UDS CAN OTA gateway.
- `third_party/iso14229/`: pinned, unmodified UDS core shared by the Gateway
  client and MCU node server.

Build and test instructions are maintained in each component directory. Build
outputs and local analysis data are intentionally excluded from this repository.
