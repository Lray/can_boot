#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "transport.h"
#include "isotp_channel.h"

static void test_recv_reports_original_datagram_length(void)
{
    int sockets[2] = {-1, -1};
    IsotpChannel channel = {-1};
    const TransportOps *transport = isotp_channel_transport_ops();
    uint8_t payload[32] = {0};
    uint8_t response[8] = {0};
    size_t response_len = 0u;
    size_t i = 0u;

    for (i = 0u; i < sizeof(payload); ++i)
    {
        payload[i] = (uint8_t)i;
    }
    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) == 0);
    channel.fd = sockets[1];
    assert(write(sockets[0], payload, sizeof(payload)) == (ssize_t)sizeof(payload));
    assert(transport->recv(&channel, response, sizeof(response), &response_len, 100u) == 0);
    assert(response_len == sizeof(payload));
    assert(memcmp(response, payload, sizeof(response)) == 0);

    assert(close(sockets[0]) == 0);
    isotp_channel_close(&channel);
}

int main(void)
{
    test_recv_reports_original_datagram_length();
    return 0;
}
