#ifndef ULOG_CAN_H
#define ULOG_CAN_H

#include <stdbool.h>

#include "301/CO_driver.h"

/** Register non-blocking raw-CAN log output after CAN enters normal mode. */
bool ULogCan_Init(CO_CANmodule_t *CANmodule, CO_CANtx_t *tx_buffer);

/** Submit one queued raw-CAN log fragment without blocking the caller. */
bool ULogCan_Poll(void);

#endif /* ULOG_CAN_H */
