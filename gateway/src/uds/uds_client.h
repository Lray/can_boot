#ifndef UDS_CLIENT_H
#define UDS_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "profile.h"
#include "shared/uds_protocol.h"
#include "transport.h"

#define UDS_ERR_TRANSPORT (-1)
#define UDS_ERR_TIMEOUT (-2)
#define UDS_ERR_NEGATIVE_RESPONSE (-3)
#define UDS_ERR_MALFORMED_RESPONSE (-4)
#define UDS_ERR_UNEXPECTED_RESPONSE (-5)
#define UDS_ERR_BUFFER_TOO_SMALL (-6)
#define UDS_ERR_RESPONSE_PENDING_LIMIT (-7)
#define UDS_ERR_INVALID_ARG (-8)




typedef struct 
{
    const TransportOps *transport;
    void *transport_ctx;
    uint8_t last_nrc;
    /* P2ServerMax: maximum wait for a normal MCU response, in milliseconds. */
    uint32_t p2_server_max_ms;
    /* P2*ServerMax: maximum wait after NRC 0x78 ResponsePending, in milliseconds. */
    uint32_t p2_star_server_max_ms;
    /* Absolute monotonic deadline for the current complete update operation. */
    uint64_t operation_deadline_ms;
}UdsClient;


/** Reopen the transport and reinitialize the client after an MCU reset. */
typedef int (*UdsReconnectFn_t)(void *ctx, UdsClient *client);

/**
 * Initialize a client and attach its transport dependency.
 *
 * @param client Non-NULL caller-owned client storage.
 * @param transport Transport operations.
 * @param transport_ctx Opaque context passed to transport operations.
 */
void uds_client_init(UdsClient *client, const TransportOps *transport,
                             void *transport_ctx);
/** Bound every subsequent transaction by one absolute monotonic deadline. */
void uds_client_set_operation_deadline(UdsClient *client, uint64_t deadline_ms);
/**
 * Check whether a client has both required transport operations.
 *
 * @param client Client to inspect; may be NULL.
 * @return Nonzero when send and receive operations are available; otherwise 0.
 */
int uds_client_is_ready(const UdsClient *client);
/**
 * Validate a complete 0x50 response and adopt its P2ServerMax/P2*ServerMax
 * values. P2* is decoded from ISO 14229's 10 ms wire unit.
 */
int uds_enter_session(UdsClient *client, uint8_t session_type);
int uds_tester_present(UdsClient *client);
int uds_security_request_seed(UdsClient *client, uint8_t *seed_out, size_t seed_cap,
                                      size_t *seed_len_out);
int uds_security_send_token(UdsClient *client, const uint8_t *token,
                                     size_t token_len);
/** Start RoutineControl 0xFF00 (EraseMemory) to erase the inactive MCU OTA slot. */
int uds_erase_memory(UdsClient *client);
/**
 * Poll the EraseMemory routine results.
 *
 * While erasing, the MCU answers NRC 0x24 (rc = UDS_ERR_NEGATIVE_RESPONSE).
 * On completion, complete_out is true for the 0x00000000 record. A flash
 * failure is reported as NRC 0x72.
 */
int uds_erase_memory_results(UdsClient *client, bool *complete_out);
/**
 * Read a DID and copy only its positive-response payload.
 *
 * @param client Ready UDS client.
 * @param did Data identifier to request.
 * @param data_out Caller-owned payload buffer.
 * @param data_cap Capacity of data_out.
 * @param data_len_out Receives payload length on success.
 * @return 0 on success or a UDS_ERR_* value on failure.
 */
int uds_read_did(UdsClient *client, uint16_t did, uint8_t *data_out, size_t data_cap,
                         size_t *data_len_out);
/**
 * Open a prepared MCU download using ISO 14229 RequestDownload.
 *
 * @param client Ready UDS client.
 * @param image_size Complete image size in bytes.
 * @param max_block_len_out Receives maxNumberOfBlockLength, including SID and BSC.
 * @return 0 on success or a UDS_ERR_* value on failure.
 */
int uds_request_download(UdsClient *client,
                         uint32_t image_size,
                         uint16_t *max_block_len_out);
/**
 * Transfer one download block using the supplied sequence counter.
 *
 * @param client Ready UDS client.
 * @param block_sequence Expected MCU block sequence counter.
 * @param data Payload bytes; may be NULL only when data_len is zero.
 * @param data_len Payload length, limited to TRANSFER_BLOCK_PAYLOAD.
 * @return 0 on success or a UDS_ERR_* value on failure.
 */
int uds_transfer_data(UdsClient *client, uint8_t block_sequence, const uint8_t *data,
                              uint16_t data_len);
int uds_request_transfer_exit(UdsClient *client);
/**
 * Request an immediate hard MCU reset and revert session state locally.
 *
 * @param client Ready UDS client.
 * @return 0 on success, UDS_ERR_TIMEOUT when the reset was acknowledged-but
 *         response lost, or another UDS_ERR_* value on failure.
 */
int uds_mcu_reset_hard(UdsClient *client);
/**
 * Check whether a transaction result is a UDS transport-layer error.
 *
 * @param rc Transaction result.
 * @return Nonzero for any UDS_ERR_* code from UDS_ERR_TRANSPORT down to
 *         UDS_ERR_RESPONSE_PENDING_LIMIT; otherwise 0.
 */
int uds_is_transport_error(int rc);

#endif
