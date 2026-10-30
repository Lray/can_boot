#ifndef ISOTP_STM32_H
#define ISOTP_STM32_H

#include "301/CO_driver.h"

typedef struct
{
    CO_CANmodule_t *module;
    CO_CANtx_t *tx_buffer;
} isotp_stm32_t;

#endif /* ISOTP_STM32_H */
