#include "uds_client.h"

#include <string.h>

#include "byte_order.h"
#include "profile.h"
#include "uds_transaction.h"
#include "util.h"

#define UDS_MAX_RESPONSE 512u

void uds_client_init(UdsClient *client, const TransportOps *transport,
                             void *transport_ctx)
{
    if (client == NULL)
    {
        return;
    }
    memset(client, 0, sizeof(*client));
    client->transport = transport;
    client->transport_ctx = transport_ctx;
    client->p2_server_max_ms = P2_SERVER_DEFAULT_MS;
    client->p2_star_server_max_ms = P2_STAR_SERVER_DEFAULT_MS;
}

int uds_client_is_ready(const UdsClient *client)
{
    return client != NULL && client->transport != NULL && client->transport->send != NULL &&
           client->transport->recv != NULL;
}

static int validate_session_control_response(const uint8_t *response, size_t response_len,
                                             uint8_t requested_session)
{
    if (response_len != UDS_SESSION_CONTROL_RESPONSE_LENGTH)
    {
        return UDS_ERR_MALFORMED_RESPONSE;
    }
    if (response[0] != SID_DIAGNOSTIC_SESSION_CONTROL_POS ||
        response[1] != (requested_session & UDS_SUBFUNCTION_VALUE_MASK))
    {
        return UDS_ERR_UNEXPECTED_RESPONSE;
    }
    return 0;
}

static int uds_client_accept_session_control_response(UdsClient *client, uint8_t requested_session,
                                                      const uint8_t *response,
                                                      size_t response_len)
{
    int rc = validate_session_control_response(response, response_len, requested_session);

    if (rc != 0)
    {
        return rc;
    }

    client->p2_server_max_ms = byte_order_get_u16_be(response + 2u);
    client->p2_star_server_max_ms =
        ((uint32_t)byte_order_get_u16_be(response + 4u) * P2_STAR_SERVER_WIRE_UNIT_MS);
    return 0;
}

/*
 * SID: 0x10 DiagnosticSessionControl - switch the ECU diagnostic session.
 *
 * Request wire format: 0x10 <session_type>.
 * The OTA worker sends 0x10 03 for Extended Session, followed by 0x10 02 for
 * Programming Session. 0x10 01 would return to Default Session.
 *
 * session_type values:
 *   SESSION_DEFAULT    (0x01): normal diagnostic operation.
 *   SESSION_PROGRAMMING (0x02): ECU reprogramming and download operations.
 *   SESSION_EXTENDED  (0x03): extended diagnostics and OTA preparation.
 *
 * On success, the client adopts the ECU's P2ServerMax and P2*ServerMax
 * timing parameters from the positive response.
 */
int uds_enter_session(UdsClient *client, uint8_t session_type)
{
    uint8_t request[2] = {0};
    uint8_t response[UDS_MAX_RESPONSE] = {0};
    size_t response_len = 0;
    int rc = 0;

    request[0] = SID_DIAGNOSTIC_SESSION_CONTROL;
    request[1] = session_type;

    rc = uds_transaction_request(client, request, sizeof(request), response,
                                 sizeof(response), &response_len);
    if (rc != 0)
    {
        return rc;
    }
    return uds_client_accept_session_control_response(client, session_type, response, response_len);
}

/*
 * SID: 0x3E TesterPresent - keep the diagnostic session alive.
 * Request wire format: 0x3E 00 (SID 0x3E, zero sub-function).
 */
int uds_tester_present(UdsClient *client)
{
    uint8_t request[2] = {0};
    uint8_t response[UDS_MAX_RESPONSE] = {0};
    size_t response_len = 0;
    int rc = 0;

    request[0] = SID_TESTER_PRESENT;
    request[1] = 0x00u;

    rc = uds_transaction_request(client, request, sizeof(request), response,
                                 sizeof(response), &response_len);
    if (rc != 0)
    {
        return rc;
    }
    if (response_len < 2u || response[1] != 0x00u)
    {
        return UDS_ERR_UNEXPECTED_RESPONSE;
    }
    return 0;
}

/*
 * SID: 0x27 SecurityAccess - request the programming seed.
 * Request wire format: 0x27 01 (programming seed sub-function).
 */
int uds_security_request_seed(UdsClient *client, uint8_t *seed_out, size_t seed_cap,
                                      size_t *seed_len_out)
{
    uint8_t request[2] = {
        SID_SECURITY_ACCESS,
        SECURITY_ACCESS_LEVEL_PROGRAMMING_SEED,
    };
    uint8_t response[2u + SECURITY_ACCESS_SEED_MAX_SIZE] = {0};
    size_t response_len = 0u;
    size_t seed_len = 0u;
    int rc = 0;

    if (!uds_client_is_ready(client) || seed_out == NULL || seed_len_out == NULL)
    {
        return UDS_ERR_INVALID_ARG;
    }
    *seed_len_out = 0u;
    rc = uds_transaction_request(client, request, sizeof(request), response,
                                 sizeof(response), &response_len);
    if (rc != 0)
    {
        secure_zero(response, sizeof(response));
        return rc;
    }
    if (response_len <= 2u || response[1] != SECURITY_ACCESS_LEVEL_PROGRAMMING_SEED)
    {
        secure_zero(response, sizeof(response));
        return UDS_ERR_UNEXPECTED_RESPONSE;
    }
    seed_len = response_len - 2u;
    if (seed_len > seed_cap)
    {
        secure_zero(response, sizeof(response));
        return UDS_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(seed_out, response + 2u, seed_len);
    *seed_len_out = seed_len;
    secure_zero(response, sizeof(response));
    return 0;
}

/*
 * SID: 0x27 SecurityAccess - send the programming key/token to unlock the ECU.
 * Request wire format: 0x27 02 <token> (programming key sub-function followed
 * by the signer-produced token bytes).
 */
int uds_security_send_token(UdsClient *client, const uint8_t *token,
                                     size_t token_len)
{
    uint8_t request[2u + SECURITY_ACCESS_TOKEN_MAX_SIZE];
    uint8_t response[8] = {0};
    size_t response_len = 0u;
    int rc = 0;

    if (!uds_client_is_ready(client) || token == NULL || token_len == 0u)
    {
        return UDS_ERR_INVALID_ARG;
    }
    if (token_len > SECURITY_ACCESS_TOKEN_MAX_SIZE)
    {
        return UDS_ERR_BUFFER_TOO_SMALL;
    }
    request[0] = SID_SECURITY_ACCESS;
    request[1] = SECURITY_ACCESS_LEVEL_PROGRAMMING_KEY;
    memcpy(request + 2u, token, token_len);
    rc = uds_transaction_request(client, request, token_len + 2u, response,
                                 sizeof(response), &response_len);
    secure_zero(request, sizeof(request));
    if (rc != 0)
    {
        secure_zero(response, sizeof(response));
        return rc;
    }
    if (response_len != 2u || response[1] != SECURITY_ACCESS_LEVEL_PROGRAMMING_KEY)
    {
        secure_zero(response, sizeof(response));
        return UDS_ERR_UNEXPECTED_RESPONSE;
    }
    secure_zero(response, sizeof(response));
    return 0;
}

int uds_prepare_download(UdsClient *client,
                         const uint8_t payload_id[PAYLOAD_ID_SIZE],
                         uint32_t image_size)
{
    uint8_t request[UDS_PREPARE_DOWNLOAD_ROUTINE_REQUEST_LEN] = {0};
    uint8_t response[UDS_MAX_RESPONSE] = {0};
    size_t response_len = 0u;
    int rc = 0;

    if (!uds_client_is_ready(client) || payload_id == NULL || image_size == 0u)
    {
        return UDS_ERR_INVALID_ARG;
    }

    request[0] = SID_ROUTINE_CONTROL;
    request[1] = ROUTINE_CONTROL_START;
    request[2] = (uint8_t)(ROUTINE_ID_PREPARE_DOWNLOAD >> 8);
    request[3] = (uint8_t)ROUTINE_ID_PREPARE_DOWNLOAD;
    byte_order_put_u32_be(request + UDS_PREPARE_DOWNLOAD_ROUTINE_SIZE_OFFSET,
                          image_size);
    memcpy(request + UDS_PREPARE_DOWNLOAD_ROUTINE_PAYLOAD_ID_OFFSET,
           payload_id,
           PAYLOAD_ID_SIZE);

    rc = uds_transaction_request(client, request, sizeof(request), response,
                                 sizeof(response), &response_len);
    if (rc != 0)
    {
        return rc;
    }
    if ((response_len != UDS_ROUTINE_CONTROL_REQUEST_LEN) ||
        (response[1] != ROUTINE_CONTROL_START) ||
        (response[2] != request[2]) || (response[3] != request[3]))
    {
        return UDS_ERR_UNEXPECTED_RESPONSE;
    }
    return 0;
}

int uds_prepare_download_ready(UdsClient *client, bool *ready_out)
{
    uint8_t request[UDS_ROUTINE_CONTROL_REQUEST_LEN] = {0};
    uint8_t response[UDS_MAX_RESPONSE] = {0};
    size_t response_len = 0u;
    int rc = 0;

    if (!uds_client_is_ready(client) || ready_out == NULL)
    {
        return UDS_ERR_INVALID_ARG;
    }
    *ready_out = false;
    request[0] = SID_ROUTINE_CONTROL;
    request[1] = ROUTINE_CONTROL_REQUEST_RESULTS;
    request[2] = (uint8_t)(ROUTINE_ID_PREPARE_DOWNLOAD >> 8);
    request[3] = (uint8_t)ROUTINE_ID_PREPARE_DOWNLOAD;

    rc = uds_transaction_request(client, request, sizeof(request), response,
                                 sizeof(response), &response_len);
    if (rc != 0)
    {
        return rc;
    }
    if ((response_len != UDS_PREPARE_DOWNLOAD_ROUTINE_RESULT_LEN) ||
        (response[1] != ROUTINE_CONTROL_REQUEST_RESULTS) ||
        (response[2] != request[2]) || (response[3] != request[3]))
    {
        return UDS_ERR_UNEXPECTED_RESPONSE;
    }
    if (response[4] == ROUTINE_PREPARE_DOWNLOAD_STATUS_READY)
    {
        *ready_out = true;
        return 0;
    }
    return response[4] == ROUTINE_PREPARE_DOWNLOAD_STATUS_PENDING
               ? 0
               : UDS_ERR_MALFORMED_RESPONSE;
}

/* SID: 0x22 ReadDataService (ReadDataByIdentifier) - read and copy an ECU DID payload. */
int uds_read_did(UdsClient *client, uint16_t did, uint8_t *data_out, size_t data_cap,
                         size_t *data_len_out)
{
    uint8_t request[3] = {0};
    uint8_t response[UDS_MAX_RESPONSE] = {0};
    size_t response_len = 0u;
    size_t payload_len = 0u;
    int rc = UDS_ERR_MALFORMED_RESPONSE;

    if (data_len_out == NULL || (data_cap > 0u && data_out == NULL))
    {
        return UDS_ERR_INVALID_ARG;
    }
    *data_len_out = 0u;
    request[0] = SID_READ_DATA_BY_IDENTIFIER;
    request[1] = (uint8_t)(did >> 8);
    request[2] = (uint8_t)(did & 0xFFu);
    rc = uds_transaction_request(client, request, sizeof(request), response,
                                 sizeof(response), &response_len);
    if (rc != 0)
    {
        return rc;
    }
    if (response_len < 3u || response[1] != request[1] || response[2] != request[2])
    {
        return UDS_ERR_UNEXPECTED_RESPONSE;
    }

    payload_len = response_len - 3u;
    if (payload_len > data_cap)
    {
        return UDS_ERR_BUFFER_TOO_SMALL;
    }

    if (payload_len > 0u)
    {
        memcpy(data_out, response + 3u, payload_len);
    }
    *data_len_out = payload_len;
    return 0;
}

/* SID: 0x34 RequestDownload - bind the payload identity and recover its cursor. */
int uds_request_download(UdsClient *client,
                         const uint8_t payload_id[PAYLOAD_ID_SIZE],
                         uint32_t image_size,
                         UdsDownloadResponse *response_out)
{
    uint8_t request[UDS_REQUEST_DOWNLOAD_REQUEST_LEN] = {0};
    uint8_t response[UDS_MAX_RESPONSE] = {0};
    size_t response_len = 0;
    int rc = 0;

    if (!uds_client_is_ready(client) || payload_id == NULL || response_out == NULL)
    {
        return UDS_ERR_INVALID_ARG;
    }

    request[0] = SID_REQUEST_DOWNLOAD;
    request[1] = 0x00u;
    request[2] = 0x44u;
    byte_order_put_u32_be(request + 3u, DOWNLOAD_MEMORY_ADDRESS);
    byte_order_put_u32_be(request + 7u, image_size);
    memcpy(request + UDS_REQUEST_DOWNLOAD_PAYLOAD_ID_OFFSET,
           payload_id,
           PAYLOAD_ID_SIZE);

    rc = uds_transaction_request(client, request, sizeof(request), response,
                                 sizeof(response), &response_len);
    if (rc != 0)
    {
        return rc;
    }
    if (response_len != UDS_REQUEST_DOWNLOAD_RESPONSE_LEN ||
        response[1] != DOWNLOAD_MAX_BLOCK_LEN_FORMAT_ID)
    {
        return UDS_ERR_MALFORMED_RESPONSE;
    }

response_out->max_block_len = byte_order_get_u16_be(
        response + UDS_REQUEST_DOWNLOAD_RESPONSE_MAX_BLOCK_OFFSET);
    response_out->target_slot =
        response[UDS_REQUEST_DOWNLOAD_RESPONSE_TARGET_SLOT_OFFSET];
    response_out->resume_offset = byte_order_get_u32_be(
        response + UDS_REQUEST_DOWNLOAD_RESPONSE_RESUME_OFFSET);
    return 0;
}

/* SID: 0x36 TransferData - transfer one ordered image data block. */
int uds_transfer_data(UdsClient *client, uint8_t block_sequence, const uint8_t *data,
                              uint16_t data_len)
{
    uint8_t request[2u + TRANSFER_BLOCK_PAYLOAD] = {0};
    uint8_t response[UDS_MAX_RESPONSE] = {0};
    size_t response_len = 0;
    int rc = 0;

    if (data_len > 0u && data == NULL)
    {
        return UDS_ERR_INVALID_ARG;
    }
    if (data_len > TRANSFER_BLOCK_PAYLOAD)
    {
        return UDS_ERR_BUFFER_TOO_SMALL;
    }

    request[0] = SID_TRANSFER_DATA;
    request[1] = block_sequence;
    if (data_len > 0u)
    {
        memcpy(request + 2u, data, data_len);
    }

    rc = uds_transaction_request(client, request, (size_t)data_len + 2u, response,
                                 sizeof(response), &response_len);
    if (rc != 0)
    {
        return rc;
    }
    if (response_len != 2u || response[1] != block_sequence)
    {
        return UDS_ERR_UNEXPECTED_RESPONSE;
    }
    return 0;
}

/* SID: 0x37 RequestTransferExit - finish the active data transfer. */
int uds_request_transfer_exit(UdsClient *client)
{
    uint8_t request[1] = {0};
    uint8_t response[UDS_MAX_RESPONSE] = {0};
    size_t response_len = 0;
    int rc = 0;

    request[0] = SID_REQUEST_TRANSFER_EXIT;

    rc = uds_transaction_request(client, request, sizeof(request), response,
                                 sizeof(response), &response_len);
    if (rc != 0)
    {
        return rc;
    }
    return response_len == 1u ? 0 : UDS_ERR_MALFORMED_RESPONSE;
}

/*
 * SID: 0x11 ECUReset - request a hard ECU reset.
 * Request wire format: 0x11 01 (hard-reset sub-function).
 */
int uds_ecu_reset_hard(UdsClient *client)
{
    uint8_t request[2] = {0};
    uint8_t response[UDS_MAX_RESPONSE] = {0};
    size_t response_len = 0;
    int rc = 0;

    request[0] = SID_ECU_RESET;
    request[1] = SUB_HARD_RESET;

    rc = uds_transaction_request(client, request, sizeof(request), response,
                                 sizeof(response), &response_len);
    if (rc != 0)
    {
        return rc;
    }
    if (response_len != 2u || response[1] != SUB_HARD_RESET)
    {
        return UDS_ERR_UNEXPECTED_RESPONSE;
    }
    client->p2_server_max_ms = P2_SERVER_DEFAULT_MS;
    client->p2_star_server_max_ms = P2_STAR_SERVER_DEFAULT_MS;
    return 0;
}

int uds_is_transport_error(int rc)
{
    return rc <= UDS_ERR_TRANSPORT && rc >= UDS_ERR_RESPONSE_PENDING_LIMIT;
}
