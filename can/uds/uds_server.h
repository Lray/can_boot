#ifndef UDS_SERVER_H
#define UDS_SERVER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool (*send)(const uint8_t *payload, uint16_t length);
    bool (*response_pending)(void);
} uds_transport_t;

void UDS_Init(const uds_transport_t *transport);
void UDS_Dispatch(const uint8_t *request, uint16_t length);
void UDS_Poll(uint32_t now_ms);

/** Consume an MCU reset accepted after its positive UDS response was sent. */
bool UDS_ConsumeAcceptedReset(void);

#endif /* UDS_SERVER_H */
