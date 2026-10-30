#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "profile.h"
#include "uds_client.h"
#include "isotp_channel.h"
#include "security_access.h"
#include "security_token_tee.h"

static void print_hex(const uint8_t *data, size_t data_len)
{
    size_t i;

    for (i = 0; i < data_len; ++i) {
        printf("%02X", data[i]);
        if (i + 1u < data_len) {
            printf(" ");
        }
    }
}

int main(int argc, char **argv)
{
    const char *ifname = argc > 1 ? argv[1] : "awlink0";
    IsotpChannelConfig config;
    IsotpChannel channel = {-1};
    UdsClient client;
    SecurityTokenTee_t *token_tee = NULL;
    int rc;

    isotp_channel_default_config(&config);
    if (isotp_channel_open(&channel, ifname, &config) != 0) {
        perror("isotp_channel_open");
        return 1;
    }
    uds_client_init(&client, isotp_channel_transport_ops(), &channel);

    printf("== enter programming ==\n");
    rc = uds_enter_session(&client, SESSION_PROGRAMMING);
    printf("programming rc=%d nrc=0x%02X\n", rc, client.last_nrc);
    printf("== tester present ==\n");
    rc = uds_tester_present(&client);
    printf("tp rc=%d nrc=0x%02X\n", rc, client.last_nrc);

    printf("== security access unlock ==\n");
    rc = security_token_tee_open(&token_tee);
    if (rc == 0) {
        rc = security_access_unlock(&client, token_tee);
    }
    printf("unlock rc=%d nrc=0x%02X\n", rc, client.last_nrc);
    if (rc == 0) {
        printf("SECURITY ACCESS PASS\n");
    } else {
        printf("SECURITY ACCESS FAIL\n");
    }
    security_token_tee_close(token_tee);
    isotp_channel_close(&channel);
    return rc == 0 ? 0 : 1;
}
