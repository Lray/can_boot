#include "watchdog.h"

#include <stdint.h>

#include "main.h"

/*
 * Refresh the bootloader-armed independent watchdog. IWDG->KR access does not
 * require a configured HAL handle; the register write is enough. If the
 * watchdog was never started (standalone application debug), the write is
 * harmless.
 */
void Watchdog_Feed(void)
{
    IWDG->KR = 0xAAAAU;
}
