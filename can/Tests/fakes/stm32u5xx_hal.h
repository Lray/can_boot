#ifndef STM32U5XX_HAL_H
#define STM32U5XX_HAL_H

#include <stdint.h>

#define FDCAN 1

typedef struct
{
    void *Instance;
} FDCAN_HandleTypeDef;

uint32_t HAL_GetTick(void);

#endif /* STM32U5XX_HAL_H */
