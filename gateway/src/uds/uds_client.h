#ifndef GATEWAY_UDS_CLIENT_H
#define GATEWAY_UDS_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iso14229.h"
#include "transport.h"

typedef struct
{
    UDSTp_t tp;
    UDSClient_t upstream;
    const TransportOps *transport;
    void *transport_ctx;
    uint64_t operation_deadline_ms;
    uint16_t max_block_length;
    uint8_t last_nrc;
    bool request_idle;
} UdsClient;

/** Reopen the transport and reinitialize the client after an MCU reset. */
typedef int (*UdsReconnectFn_t)(void *ctx, UdsClient *client);

void uds_client_init(UdsClient *client, const TransportOps *transport,
                     void *transport_ctx);
void uds_client_set_operation_deadline(UdsClient *client, uint64_t deadline_ms);
int uds_client_is_ready(const UdsClient *client);

int uds_enter_session(UdsClient *client, uint8_t session_type);
int uds_tester_present(UdsClient *client);
int uds_security_request_seed(UdsClient *client, uint8_t *seed_out,
                              size_t seed_cap, size_t *seed_len_out);
int uds_security_send_token(UdsClient *client, const uint8_t *token,
                            size_t token_len);
int uds_erase_memory(UdsClient *client);
int uds_erase_memory_results(UdsClient *client, bool *complete_out);
int uds_read_did(UdsClient *client, uint16_t did, uint8_t *data_out,
                 size_t data_cap, size_t *data_len_out);
int uds_request_download(UdsClient *client, uint32_t image_size,
                         uint16_t *max_block_len_out);
int uds_transfer_data(UdsClient *client, uint8_t block_sequence,
                      const uint8_t *data, uint16_t data_len);
int uds_request_transfer_exit(UdsClient *client);
int uds_mcu_reset_hard(UdsClient *client);

#endif
