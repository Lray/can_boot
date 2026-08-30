#include "isotp_channel.h"

#include <linux/can.h>
#include <linux/can/isotp.h>
#include <net/if.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "profile.h"

void isotp_channel_default_config(IsotpChannelConfig *config)
{
    if (config == NULL) {
        return;
    }
    config->request_id = CAN_ID_UDS_REQUEST;
    config->response_id = CAN_ID_UDS_RESPONSE;
    config->block_size = ISOTP_BLOCK_SIZE;
    config->stmin_raw = ISOTP_STMIN_MS;
}

static int isotp_channel_validate_config(const IsotpChannelConfig *config)
{
    if (config == NULL)
    {
        return -1;
    }
    if (config->request_id > CAN_SFF_MASK || config->response_id > CAN_SFF_MASK)
    {
        return -1;
    }
    if (config->stmin_raw > 0x7Fu &&
        (config->stmin_raw < 0xF1u || config->stmin_raw > 0xF9u))
    {
        return -1;
    }
    return 0;
}

int isotp_channel_open(IsotpChannel *channel, const char *ifname, const IsotpChannelConfig *config)
{
    int fd;
    struct ifreq ifr;
    struct sockaddr_can addr;
    struct can_isotp_fc_options fcopts;

    if (channel == NULL || ifname == NULL || *ifname == '\0' || isotp_channel_validate_config(config) != 0) {
        return -1;
    }

    fd = socket(PF_CAN, SOCK_DGRAM, CAN_ISOTP);
    if (fd < 0) {
        return -1;
    }

    memset(&fcopts, 0, sizeof(fcopts));
    fcopts.bs = config->block_size;
    fcopts.stmin = config->stmin_raw;
    fcopts.wftmax = 0;
    if (setsockopt(fd, SOL_CAN_ISOTP, CAN_ISOTP_RECV_FC, &fcopts, sizeof(fcopts)) < 0) {
        close(fd);
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1u);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    addr.can_addr.tp.rx_id = config->response_id;
    addr.can_addr.tp.tx_id = config->request_id;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    channel->fd = fd;
    return 0;
}

void isotp_channel_close(IsotpChannel *channel)
{
    if (channel != NULL && channel->fd >= 0) {
        close(channel->fd);
        channel->fd = -1;
    }
}

static int isotp_transport_send(void *ctx, const uint8_t *data, size_t data_len)
{
    IsotpChannel *channel = (IsotpChannel *)ctx;

    if (channel == NULL || channel->fd < 0 || data == NULL || data_len == 0u) {
        return -1;
    }

    return write(channel->fd, data, data_len) == (ssize_t)data_len ? 0 : -1;
}

static int isotp_transport_recv(void *ctx, uint8_t *data, size_t data_cap, size_t *data_len,
                                uint32_t timeout_ms)
{
    IsotpChannel *channel = (IsotpChannel *)ctx;
    struct pollfd pfd = {0};
    struct iovec iov = {0};
    struct msghdr message = {0};
    int poll_rc = -1;
    ssize_t nread = -1;

    if (channel == NULL || channel->fd < 0 || data == NULL || data_len == NULL || data_cap == 0u)
    {
        return -1;
    }

    pfd.fd = channel->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    poll_rc = poll(&pfd, 1, (int)timeout_ms);
    if (poll_rc <= 0)
    {
        return -1;
    }

    iov.iov_base = data;
    iov.iov_len = data_cap;
    message.msg_iov = &iov;
    message.msg_iovlen = 1u;
    nread = recvmsg(channel->fd, &message, MSG_TRUNC);
    if (nread <= 0)
    {
        return -1;
    }

    *data_len = (size_t)nread;
    return 0;
}

const TransportOps *isotp_channel_transport_ops(void)
{
    static const TransportOps ops = {
        isotp_transport_send,
        isotp_transport_recv,
    };

    return &ops;
}
