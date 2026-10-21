#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdint.h>

/** Zero a memory region without the compiler optimizing the write away. */
void secure_zero(void *data, size_t length);

/**
 * Return the monotonic clock in milliseconds, or zero on clock failure.
 */
uint64_t util_monotonic_ms(void);

/**
 * Sleep for delay_ms, retrying on EINTR.  Returns 0 on success or -1 on an
 * unhandled nanosleep error.
 */
int util_sleep_ms(uint32_t delay_ms);

#endif
