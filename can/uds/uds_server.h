#ifndef UDS_SERVER_H
#define UDS_SERVER_H

#include <stdbool.h>
#include <stdint.h>

struct IsoTpLink;

void UDS_ServerInit(struct IsoTpLink *transport);
void UDS_ServerDispatch(const uint8_t *request, uint16_t length);
void UDS_ServerPoll(uint32_t now_ms);

/** Consume an MCU reset accepted after its positive UDS response was sent. */
bool UDS_ServerConsumeAcceptedReset(void);

#endif /* UDS_SERVER_H */