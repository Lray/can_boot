#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <linux/can.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "socketcan_raw.h"

static int parse_hex_byte(char high, char low, uint8_t *value_out)
{
    char text[3] = {high, low, '\0'};
    char *end = NULL;
    unsigned long value = 0u;

    if (!isxdigit((unsigned char)high) || !isxdigit((unsigned char)low))
    {
        return -1;
    }
    errno = 0;
    value = strtoul(text, &end, 16);
    if (errno != 0 || end == NULL || *end != '\0' || value > 0xFFu)
    {
        return -1;
    }
    *value_out = (uint8_t)value;
    return 0;
}

static int parse_can_id(const char *text, uint32_t *can_id_out)
{
    char *end = NULL;
    unsigned long value = 0u;

    if (text == NULL || can_id_out == NULL || *text == '\0')
    {
        return -1;
    }
    errno = 0;
    value = strtoul(text, &end, 16);
    if (errno != 0 || end == NULL || *end != '\0' || value > CAN_SFF_MASK)
    {
        return -1;
    }
    *can_id_out = (uint32_t)value;
    return 0;
}

static int parse_payload_hex(const char *text, uint8_t *data_out, size_t data_cap,
                             size_t *data_len_out)
{
    size_t length = 0u;
    size_t index = 0u;

    if (text == NULL || data_out == NULL || data_len_out == NULL)
    {
        return -1;
    }
    length = strlen(text);
    if ((length % 2u) != 0u || length / 2u > data_cap)
    {
        return -1;
    }
    for (index = 0u; index < length / 2u; ++index)
    {
        if (parse_hex_byte(text[index * 2u], text[index * 2u + 1u], &data_out[index]) != 0)
        {
            return -1;
        }
    }
    *data_len_out = length / 2u;
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t can_id = 0u;
    uint8_t data[CAN_MAX_DLEN] = {0};
    size_t data_len = 0u;
    int fd = -1;

    if (argc != 4)
    {
        fprintf(stderr, "usage: %s <ifname> <can_id_hex> <payload_hex>\n", argv[0]);
        return 2;
    }

    if (parse_can_id(argv[2], &can_id) != 0)
    {
        fprintf(stderr, "invalid standard can id: %s\n", argv[2]);
        return 2;
    }

    if (parse_payload_hex(argv[3], data, sizeof(data), &data_len) != 0)
    {
        fprintf(stderr, "invalid classic can payload: %s\n", argv[3]);
        return 2;
    }

    {
        const struct can_filter filter = {
            .can_id = can_id,
            .can_mask = CAN_SFF_MASK,
        };

        fd = socketcan_raw_open_filtered(argv[1], &filter, 1u);
    }
    if (fd < 0)
    {
        perror("socketcan_raw_open_filtered");
        return 1;
    }

    if (socketcan_raw_send(fd, can_id, data, data_len) != 0)
    {
        perror("socketcan_raw_send");
        (void)close(fd);
        return 1;
    }

    printf("sent can_id=0x%03X dlc=%zu\n", can_id, data_len);
    (void)close(fd);
    return 0;
}
