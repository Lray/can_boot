#include "util.h"

#include <errno.h>
#include <stdint.h>
#include <time.h>

void secure_zero(void *data, size_t length)
{
    volatile uint8_t *cursor = (volatile uint8_t *)data;

    while (length > 0u)
    {
        *cursor++ = 0u;
        length--;
    }
}

uint64_t util_monotonic_ms(void)
{
    struct timespec now = {0};

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        return 0u;
    }
    return ((uint64_t)now.tv_sec * 1000u) + ((uint64_t)now.tv_nsec / 1000000u);
}

int util_sleep_ms(uint32_t delay_ms)
{
    struct timespec delay = {0};
    int rc = 0;

    delay.tv_sec = (time_t)(delay_ms / 1000u);
    delay.tv_nsec = (long)((delay_ms % 1000u) * 1000000u);
    do
    {
        rc = nanosleep(&delay, &delay);
    } while (rc != 0 && errno == EINTR);
    return rc != 0 ? -1 : 0;
}
