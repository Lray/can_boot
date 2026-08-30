#ifndef WATCHDOG_H
#define WATCHDOG_H

/*
 * The bootloader starts the independent watchdog before jumping to the
 * application. An IWDG cannot be stopped once started, so the application
 * only needs to refresh it periodically; it must never reconfigure or
 * reinitialize the watchdog.
 */
void Watchdog_Feed(void);

#endif
