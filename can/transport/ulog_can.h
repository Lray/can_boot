#ifndef ULOG_CAN_H
#define ULOG_CAN_H

#include <stdbool.h>

/** Register non-blocking raw-CAN log output after CAN_Start succeeds. */
bool ULogCan_Init(void);

/** Submit one queued raw-CAN log fragment without blocking the caller. */
bool ULogCan_Poll(void);

#endif /* ULOG_CAN_H */
