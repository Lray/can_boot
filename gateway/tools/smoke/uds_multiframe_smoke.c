#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "profile.h"
#include "uds_client.h"
#include "isotp_channel.h"

int main(int argc, char **argv)
{
    const char *ifname = argc > 1 ? argv[1] : "awlink0";
    IsotpChannelConfig config;
    IsotpChannel channel = {-1};
    UdsClient client;
    uint8_t request[220];
    uint8_t response[64];
    size_t response_len = 0;
    size_t i;
    int rc;

    isotp_channel_default_config(&config);
    if (isotp_channel_open(&channel, ifname, &config) != 0) {
        perror("isotp_channel_open");
        return 1;
    }
    uds_client_init(&client, isotp_channel_transport_ops(), &channel);

    /* 0x27 02 with dummy 200-byte token - isolate multi-frame send path */
    request[0] = 0x27;
    request[1] = 0x02;
    for (i = 2; i < sizeof(request); ++i) {
        request[i] = (uint8_t)(i & 0xFF);
    }
    printf("send %u bytes 0x27 02\n", (unsigned)sizeof(request));
    rc = client.transport->send(client.transport_ctx, request, sizeof(request));
    printf("send rc=%d\n", rc);

    printf("recv (expect NRC or 67 02)\n");
    rc = client.transport->recv(client.transport_ctx, response, sizeof(response),
                                &response_len, 3000);
    printf("recv rc=%d len=%u data=", rc, (unsigned)response_len);
    for (i = 0; i < response_len && i < 16; ++i) {
        printf("%02X ", response[i]);
    }
    printf("\n");
    isotp_channel_close(&channel);
    return 0;
}
