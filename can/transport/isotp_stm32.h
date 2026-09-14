#ifndef ISOTP_STM32_H
#define ISOTP_STM32_H

#include "can_driver.h"

void isotp_stm32_init(can_module_t *CANmodule, can_tx_t *tx_buffer);

#endif /* ISOTP_STM32_H */
