#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "profile.h"
#include "util.h"
#include "uds_client.h"
#include "isotp_channel.h"

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

static int read_and_print_did(UdsClient *client, uint16_t did)
{
    uint8_t data[128];
    size_t data_len = 0;
    int rc;

    rc = uds_read_did(client, did, data, sizeof(data), &data_len);
    if (rc != 0) {
        fprintf(stderr, "ReadDID 0x%04X failed rc=%d nrc=0x%02X\n", did, rc, client->last_nrc);
        return rc;
    }

    printf("ReadDID 0x%04X PASS data=", did);
    print_hex(data, data_len);
    printf("\n");
    return 0;
}

int main(int argc, char **argv)
{
    const char *ifname = argc > 1 ? argv[1] : "awlink0";
    IsotpChannelConfig config;
    IsotpChannel channel = {-1};
    UdsClient client;
    uint8_t negative_data[8];
    size_t negative_len = 0;
    uint16_t negative_did = 0xFFFFu;

    if (argc > 3) {
        negative_did = (uint16_t)strtoul(argv[3], NULL, 16);
    }

    unsigned long node_id;
    if (argc < 3 || argc > 4 || util_parse_identity(argv[2], &node_id) != 0 ||
        node_id < CAN_NODE_ID_MIN || node_id > CAN_NODE_ID_MAX) {
        fprintf(stderr, "usage: %s <ifname> <node-id> [negative-did-hex]\n", argv[0]);
        return 2;
    }
    config = (IsotpChannelConfig){.request_id = CAN_ID_UDS_REQUEST(node_id),
        .response_id = CAN_ID_UDS_RESPONSE(node_id),
        .block_size = ISOTP_BLOCK_SIZE, .stmin_raw = ISOTP_STMIN_MS};
    if (isotp_channel_open(&channel, ifname, &config) != 0) {
        perror("isotp_channel_open");
        return 1;
    }

    uds_client_init(&client, isotp_channel_transport_ops(), &channel);

    int rc;

    rc = uds_enter_session(&client, SESSION_EXTENDED);
    if (rc != 0) {
        fprintf(stderr, "DiagnosticSessionControl extended failed rc=%d nrc=0x%02X\n", rc, client.last_nrc);
        isotp_channel_close(&channel);
        return 1;
    }
    printf("DiagnosticSessionControl 0x03 PASS\n");

    rc = uds_enter_session(&client, SESSION_PROGRAMMING);
    if (rc != 0) {
        fprintf(stderr, "DiagnosticSessionControl programming failed rc=%d nrc=0x%02X\n", rc, client.last_nrc);
        isotp_channel_close(&channel);
        return 1;
    }
    printf("DiagnosticSessionControl 0x02 PASS\n");

    if (uds_tester_present(&client) != 0) {
        fprintf(stderr, "TesterPresent failed nrc=0x%02X\n", client.last_nrc);
        isotp_channel_close(&channel);
        return 1;
    }
    printf("TesterPresent PASS\n");

    if (read_and_print_did(&client, DID_LSS_IDENTITY) != 0 ||
        read_and_print_did(&client, DID_BOOT_VERSION) != 0 ||
        read_and_print_did(&client, DID_APP_VERSION) != 0 ||
        read_and_print_did(&client, DID_UPDATER_VERSION) != 0 ||
        read_and_print_did(&client, DID_ACTIVE_SLOT) != 0 ||
        read_and_print_did(&client, DID_CONFIRM_RESULT) != 0) {
        isotp_channel_close(&channel);
        return 1;
    }

    if (uds_read_did(&client, negative_did, negative_data, sizeof(negative_data), &negative_len) != UDS_ERR_NEGATIVE_RESPONSE) {
        fprintf(stderr, "negative response path failed: DID 0x%04X did not return negative response\n", negative_did);
        isotp_channel_close(&channel);
        return 1;
    }
    printf("NegativeResponse PASS sid=0x%02X nrc=0x%02X\n",
           SID_READ_DATA_BY_IDENTIFIER, client.last_nrc);
    printf("UDS PASS\n");

    isotp_channel_close(&channel);
    return 0;
}
