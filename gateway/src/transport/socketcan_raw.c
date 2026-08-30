#include "socketcan_raw.h"

#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "profile.h"

int socketcan_raw_open_filtered(
    const char *ifname,
    const struct can_filter *filters,
    size_t filter_count)
{
    int fd;
    struct ifreq ifr;
    struct sockaddr_can addr;

    if (ifname == NULL || *ifname == '\0' || filters == NULL || filter_count == 0u) {
        return -1;
    }

    fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (setsockopt(fd,
                   SOL_CAN_RAW,
                   CAN_RAW_FILTER,
                   filters,
                   (socklen_t)(filter_count * sizeof(*filters))) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

int socketcan_raw_send(int fd, uint32_t can_id, const uint8_t *data, size_t data_len)
{
    struct can_frame frame;

    if (data_len > CAN_MAX_DLEN || (data_len > 0u && data == NULL) || can_id > CAN_SFF_MASK) {
        return -1;
    }

    memset(&frame, 0, sizeof(frame));
    frame.can_id = can_id;
    frame.can_dlc = (uint8_t)data_len;
    if (data_len > 0u) {
        memcpy(frame.data, data, data_len);
    }

    return write(fd, &frame, sizeof(frame)) == (ssize_t)sizeof(frame) ? 0 : -1;
}

int socketcan_raw_recv(int fd, struct can_frame *frame_out, int timeout_ms)
{
    struct pollfd pfd;

    if (frame_out == NULL) {
        return -1;
    }

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    {
        int poll_result = poll(&pfd, 1, timeout_ms);

        if (poll_result < 0)
        {
            return -1;
        }
        if (poll_result == 0)
        {
            return SOCKETCAN_RAW_TIMEOUT;
        }
    }
    if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
    {
        errno = EIO;
        return -1;
    }

    return read(fd, frame_out, sizeof(*frame_out)) == (ssize_t)sizeof(*frame_out) ? 1 : -1;
}
