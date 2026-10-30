#ifndef UDS_SERVER_H
#define UDS_SERVER_H

#include "iso14229.h"

void UDS_Init(UDSTp_t *transport);
void UDS_Poll(uint32_t now_ms);
bool UDS_ConsumeAcceptedReset(void);

#endif
