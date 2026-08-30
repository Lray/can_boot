#ifndef CAN_FRAME_H
#define CAN_FRAME_H

#include <stdint.h>

/* CAN FD and extended-addressing metadata are intentionally out of profile. */
#define CAN_CLASSIC_MAX_DLC 8U

typedef struct
{
    uint32_t id;
    uint8_t dlc;
    uint8_t data[CAN_CLASSIC_MAX_DLC];
} can_frame_t;

#endif /* CAN_FRAME_H */
