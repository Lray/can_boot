#include "util.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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

int is_lower_hex(const char *text, size_t length)
{
    size_t index = 0u;

    if (text == NULL || strlen(text) != length)
    {
        return 0;
    }
    for (index = 0u; index < length; ++index)
    {
        if (!((text[index] >= '0' && text[index] <= '9') ||
              (text[index] >= 'a' && text[index] <= 'f')))
        {
            return 0;
        }
    }
    return 1;
}

int is_uuid_v4(const char *text)
{
    size_t index = 0u;

    if (text == NULL || strlen(text) != 36u || text[14] != '4' ||
        strchr("89ab", text[19]) == NULL)
    {
        return 0;
    }
    for (index = 0u; index < 36u; ++index)
    {
        if (index == 8u || index == 13u || index == 18u || index == 23u)
        {
            if (text[index] != '-')
            {
                return 0;
            }
        }
        else if (!((text[index] >= '0' && text[index] <= '9') ||
                   (text[index] >= 'a' && text[index] <= 'f')))
        {
            return 0;
        }
    }
    return 1;
}

int util_parse_hex(const char *text, uint8_t *output, size_t output_len)
{
    size_t index = 0u;

    if (text == NULL || output == NULL ||
        !is_lower_hex(text, output_len * 2u))
    {
        return -1;
    }
    for (index = 0u; index < output_len; ++index)
    {
        char pair[3] = {text[index * 2u], text[index * 2u + 1u], '\0'};
        char *end = NULL;
        unsigned long value = strtoul(pair, &end, 16);

        if (end == NULL || *end != '\0')
        {
            return -1;
        }
        output[index] = (uint8_t)value;
    }
    return 0;
}

int util_parse_size(const char *text, uint64_t *value_out)
{
    char *end = NULL;
    unsigned long long value;

    if (text == NULL || value_out == NULL || text[0] == '\0' ||
        (text[0] == '0' && text[1] != '\0'))
    {
        return -1;
    }
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0u)
    {
        return -1;
    }
    *value_out = (uint64_t)value;
    return 0;
}

int util_parse_identity(const char *text, unsigned long *value_out)
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL || value_out == NULL || text[0] == '\0')
    {
        return -1;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0u ||
        value > 65535u)
    {
        return -1;
    }
    *value_out = value;
    return 0;
}

int valid_ifname(const char *ifname)
{
    size_t index = 0u;
    size_t length = 0u;

    if (ifname == NULL)
    {
        return 0;
    }
    length = strlen(ifname);
    if (length == 0u || length >= 16u)
    {
        return 0;
    }
    for (index = 0u; index < length; ++index)
    {
        char value = ifname[index];

        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '_' || value == '-' ||
              value == '.'))
        {
            return 0;
        }
    }
    return 1;
}
