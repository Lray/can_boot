#ifndef UDS_SERVER_H
#define UDS_SERVER_H

#include <stdbool.h>
#include <stdint.h>

struct IsoTpLink;

void UDS_Init(struct IsoTpLink *transport);
void UDS_Dispatch(const uint8_t *request, uint16_t length);
void UDS_Poll(uint32_t now_ms);

/** Consume an ECU reset accepted after its positive UDS response was sent. */
bool UDS_ConsumeAcceptedReset(void);

#endif /* UDS_SERVER_H */
