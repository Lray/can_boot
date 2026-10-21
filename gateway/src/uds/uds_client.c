#include "uds_client.h"

#include <limits.h>
#include <string.h>

#include "byte_order.h"
#include "profile.h"
#include "util.h"

#define UDS_TRANSPORT_POLL_MS 1U

static UdsClient *uds_owner(UDSTp_t *tp)
{
    return (UdsClient *)tp;
}

static UDSErr_t gateway_tp_send(UDSTp_t *tp, const uint8_t *data,
                                size_t data_len, const UDSSDU_t *info)
{
    UdsClient *client = uds_owner(tp);

    (void)info;
    if (client->transport->send(client->transport_ctx, data, data_len) != 0)
    {
        return UDS_ERR_TPORT;
    }
    tp->status.is_sending = 0U;
    return UDS_OK;
}

static UDSErr_t gateway_tp_recv(UDSTp_t *tp, uint8_t *data, size_t data_cap,
                                size_t *data_len, UDSSDU_t *info)
{
    UdsClient *client = uds_owner(tp);
    size_t received = 0U;

    *data_len = 0U;
    if (client->transport->recv(client->transport_ctx, data, data_cap,
                                &received, UDS_TRANSPORT_POLL_MS) != 0)
    {
        return UDS_OK;
    }
    if (received > data_cap)
    {
        return UDS_ERR_BUFSIZ;
    }
    *data_len = received;
    if (info != NULL)
    {
        memset(info, 0, sizeof(*info));
        info->A_Mtype = UDS_A_MTYPE_DIAG;
        info->A_TA_Type = UDS_A_TA_TYPE_PHYSICAL;
    }
    return UDS_OK;
}

static UDSErr_t gateway_tp_poll(UDSTp_t *tp)
{
    tp->status.is_sending = 0U;
    return UDS_OK;
}

static int gateway_client_event(UDSClient_t *client, UDSEvent_t event,
                                void *event_data)
{
    UdsClient *owner = client->fn_data;

    (void)event_data;
    if (event == UDS_EVT_Idle)
    {
        owner->request_idle = true;
    }
    return 0;
}

static void init_upstream(UdsClient *client)
{
    (void)UDSClientInit(&client->upstream);
    client->upstream.tp = &client->tp;
    client->upstream.fn = gateway_client_event;
    client->upstream.fn_data = client;
    client->request_idle = true;
}

void uds_client_init(UdsClient *client, const TransportOps *transport,
                     void *transport_ctx)
{
    if (client == NULL)
    {
        return;
    }
    memset(client, 0, sizeof(*client));
    client->tp.send = gateway_tp_send;
    client->tp.recv = gateway_tp_recv;
    client->tp.poll = gateway_tp_poll;
    client->transport = transport;
    client->transport_ctx = transport_ctx;
    init_upstream(client);
}

void uds_client_set_operation_deadline(UdsClient *client, uint64_t deadline_ms)
{
    if (client != NULL)
    {
        client->operation_deadline_ms = deadline_ms;
    }
}

int uds_client_is_ready(const UdsClient *client)
{
    return client != NULL && client->transport != NULL &&
           client->transport->send != NULL && client->transport->recv != NULL;
}

static int request_preflight(const UdsClient *client)
{
    if (!uds_client_is_ready(client))
    {
        return UDS_ERR_INVALID_ARG;
    }
    if (client->operation_deadline_ms != 0U &&
        util_monotonic_ms() >= client->operation_deadline_ms)
    {
        return UDS_ERR_TIMEOUT;
    }
    return UDS_OK;
}

static int wait_for_response(UdsClient *client, UDSErr_t request_result)
{
    UDSErr_t result = request_result;

    client->last_nrc = 0U;
    client->request_idle = false;
    if (result != UDS_OK)
    {
        init_upstream(client);
        return result;
    }
    while (!client->request_idle)
    {
        if (client->operation_deadline_ms != 0U &&
            util_monotonic_ms() >= client->operation_deadline_ms)
        {
            init_upstream(client);
            return UDS_ERR_TIMEOUT;
        }
        result = UDSClientPoll(&client->upstream);
        if (result == UDS_OK ||
            result == UDS_NRC_RequestCorrectlyReceived_ResponsePending)
        {
            continue;
        }
        if (result > UDS_OK && result < UDS_ERR_TIMEOUT)
        {
            client->last_nrc = (uint8_t)result;
        }
        return result;
    }
    return UDS_OK;
}

static int validate_subfunction_response(const UdsClient *client,
                                         uint8_t response_sid,
                                         uint8_t subfunction,
                                         size_t response_len)
{
    if (client->upstream.recv_size != response_len)
    {
        return UDS_ERR_RESP_TOO_SHORT;
    }
    if (client->upstream.recv_buf[0] != response_sid)
    {
        return UDS_ERR_SID_MISMATCH;
    }
    if (client->upstream.recv_buf[1] != subfunction)
    {
        return UDS_ERR_SUBFUNCTION_MISMATCH;
    }
    return UDS_OK;
}

int uds_enter_session(UdsClient *client, uint8_t session_type)
{
    int result = request_preflight(client);

    if (result != UDS_OK)
    {
        return result;
    }
    result = wait_for_response(
        client, UDSSendDiagSessCtrl(&client->upstream, session_type));
    if (result != UDS_OK)
    {
        return result;
    }
    return validate_subfunction_response(
        client, SID_DIAGNOSTIC_SESSION_CONTROL_POS,
        (uint8_t)(session_type & UDS_SUBFUNCTION_VALUE_MASK),
        UDS_SESSION_CONTROL_RESPONSE_LENGTH);
}

int uds_tester_present(UdsClient *client)
{
    int result = request_preflight(client);

    if (result != UDS_OK)
    {
        return result;
    }
    result = wait_for_response(client, UDSSendTesterPresent(&client->upstream));
    if (result != UDS_OK)
    {
        return result;
    }
    return validate_subfunction_response(
        client, SID_TESTER_PRESENT_POS, 0U, 2U);
}

int uds_security_request_seed(UdsClient *client, uint8_t *seed_out,
                              size_t seed_cap, size_t *seed_len_out)
{
    struct SecurityAccessResponse response = {0};
    int result;

    if (seed_out == NULL || seed_len_out == NULL)
    {
        return UDS_ERR_INVALID_ARG;
    }
    result = request_preflight(client);
    if (result != UDS_OK)
    {
        return result;
    }
    *seed_len_out = 0U;
    result = wait_for_response(
        client, UDSSendSecurityAccess(
                    &client->upstream,
                    SECURITY_ACCESS_LEVEL_PROGRAMMING_SEED, NULL, 0U));
    if (result == UDS_OK)
    {
        result = UDSUnpackSecurityAccessResponse(&client->upstream, &response);
    }
    if (result == UDS_OK &&
        response.securityAccessType !=
            SECURITY_ACCESS_LEVEL_PROGRAMMING_SEED)
    {
        result = UDS_ERR_SUBFUNCTION_MISMATCH;
    }
    if (result == UDS_OK && response.securitySeedLength > seed_cap)
    {
        result = UDS_ERR_BUFSIZ;
    }
    if (result == UDS_OK)
    {
        memcpy(seed_out, response.securitySeed, response.securitySeedLength);
        *seed_len_out = response.securitySeedLength;
    }
    secure_zero(client->upstream.recv_buf,
                sizeof(client->upstream.recv_buf));
    return result;
}

int uds_security_send_token(UdsClient *client, const uint8_t *token,
                            size_t token_len)
{
    int result;

    if (token == NULL || token_len == 0U || token_len > UINT16_MAX)
    {
        return UDS_ERR_INVALID_ARG;
    }
    result = request_preflight(client);
    if (result != UDS_OK)
    {
        return result;
    }
    result = wait_for_response(
        client, UDSSendSecurityAccess(
                    &client->upstream,
                    SECURITY_ACCESS_LEVEL_PROGRAMMING_KEY,
                    token, (uint16_t)token_len));
    if (result == UDS_OK)
    {
        result = validate_subfunction_response(
            client, SID_SECURITY_ACCESS_POS,
            SECURITY_ACCESS_LEVEL_PROGRAMMING_KEY, 2U);
    }
    secure_zero(client->upstream.send_buf,
                sizeof(client->upstream.send_buf));
    secure_zero(client->upstream.recv_buf,
                sizeof(client->upstream.recv_buf));
    return result;
}

static int run_erase_routine(UdsClient *client, uint8_t control_type)
{
    return wait_for_response(
        client, UDSSendRoutineCtrl(&client->upstream, control_type,
                                   ROUTINE_ID_ERASE_MEMORY, NULL, 0U));
}

int uds_erase_memory(UdsClient *client)
{
    int result = request_preflight(client);

    if (result != UDS_OK)
    {
        return result;
    }
    result = run_erase_routine(client, ROUTINE_CONTROL_START);
    if (result != UDS_OK)
    {
        return result;
    }
    return validate_subfunction_response(
        client, SID_ROUTINE_CONTROL_POS, ROUTINE_CONTROL_START,
        UDS_ROUTINE_CONTROL_REQUEST_LEN);
}

int uds_erase_memory_results(UdsClient *client, bool *complete_out)
{
    struct RoutineControlResponse response = {0};
    int result;

    if (complete_out == NULL)
    {
        return UDS_ERR_INVALID_ARG;
    }
    result = request_preflight(client);
    if (result != UDS_OK)
    {
        return result;
    }
    *complete_out = false;
    result = run_erase_routine(client, ROUTINE_CONTROL_REQUEST_RESULTS);
    if (result == UDS_OK)
    {
        result = UDSUnpackRoutineControlResponse(
            &client->upstream, &response);
    }
    if (result == UDS_OK &&
        (response.routineControlType != ROUTINE_CONTROL_REQUEST_RESULTS ||
         response.routineIdentifier != ROUTINE_ID_ERASE_MEMORY))
    {
        result = UDS_ERR_SUBFUNCTION_MISMATCH;
    }
    if (result == UDS_OK &&
        response.routineStatusRecordLength != sizeof(uint32_t))
    {
        result = UDS_ERR_RESP_TOO_SHORT;
    }
    if (result == UDS_OK &&
        byte_order_get_u32_be(response.routineStatusRecord) !=
            ROUTINE_ERASE_RESULT_OK)
    {
        result = UDS_FAIL;
    }
    if (result == UDS_OK)
    {
        *complete_out = true;
    }
    return result;
}

int uds_read_did(UdsClient *client, uint16_t did, uint8_t *data_out,
                 size_t data_cap, size_t *data_len_out)
{
    size_t payload_len;
    int result;

    if (data_len_out == NULL || (data_cap != 0U && data_out == NULL))
    {
        return UDS_ERR_INVALID_ARG;
    }
    result = request_preflight(client);
    if (result != UDS_OK)
    {
        return result;
    }
    *data_len_out = 0U;
    result = wait_for_response(
        client, UDSSendRDBI(&client->upstream, &did, 1U));
    if (result != UDS_OK)
    {
        return result;
    }
    if (client->upstream.recv_size < 3U)
    {
        return UDS_ERR_RESP_TOO_SHORT;
    }
    if (client->upstream.recv_buf[0] != SID_READ_DATA_BY_IDENTIFIER_POS)
    {
        return UDS_ERR_SID_MISMATCH;
    }
    if (byte_order_get_u16_be(&client->upstream.recv_buf[1]) != did)
    {
        return UDS_ERR_DID_MISMATCH;
    }
    payload_len = client->upstream.recv_size - 3U;
    if (payload_len > data_cap)
    {
        return UDS_ERR_BUFSIZ;
    }
    memcpy(data_out, &client->upstream.recv_buf[3], payload_len);
    *data_len_out = payload_len;
    return UDS_OK;
}

int uds_request_download(UdsClient *client, uint32_t image_size,
                         uint16_t *max_block_len_out)
{
    struct RequestDownloadResponse response = {0};
    int result;

    if (max_block_len_out == NULL)
    {
        return UDS_ERR_INVALID_ARG;
    }
    result = request_preflight(client);
    if (result != UDS_OK)
    {
        return result;
    }
    result = wait_for_response(
        client, UDSSendRequestDownload(
                    &client->upstream, DOWNLOAD_DATA_FORMAT_ID,
                    DOWNLOAD_ADDR_LEN_FORMAT_ID, DOWNLOAD_MEMORY_ADDRESS,
                    image_size));
    if (result == UDS_OK)
    {
        result = UDSUnpackRequestDownloadResponse(
            &client->upstream, &response);
    }
    if (result == UDS_OK &&
        (response.maxBlockLength > UINT16_MAX ||
         response.maxBlockLength <= 2U))
    {
        result = UDS_ERR_BUFSIZ;
    }
    if (result == UDS_OK)
    {
        client->max_block_length = (uint16_t)response.maxBlockLength;
        *max_block_len_out = client->max_block_length;
    }
    return result;
}

int uds_transfer_data(UdsClient *client, uint8_t block_sequence,
                      const uint8_t *data, uint16_t data_len)
{
    int result;

    if ((data_len != 0U && data == NULL) ||
        (client != NULL && client->max_block_length <= 2U))
    {
        return UDS_ERR_INVALID_ARG;
    }
    result = request_preflight(client);
    if (result != UDS_OK)
    {
        return result;
    }
    result = wait_for_response(
        client, UDSSendTransferData(
                    &client->upstream, block_sequence,
                    client->max_block_length, data, data_len));
    if (result != UDS_OK)
    {
        return result;
    }
    return validate_subfunction_response(
        client, SID_TRANSFER_DATA_POS, block_sequence, 2U);
}

int uds_request_transfer_exit(UdsClient *client)
{
    int result = request_preflight(client);

    if (result != UDS_OK)
    {
        return result;
    }
    result = wait_for_response(
        client, UDSSendRequestTransferExit(&client->upstream));
    if (result != UDS_OK)
    {
        return result;
    }
    client->max_block_length = 0U;
    return client->upstream.recv_size == 1U
               ? UDS_OK
               : UDS_ERR_RESP_TOO_SHORT;
}

int uds_mcu_reset_hard(UdsClient *client)
{
    int result = request_preflight(client);

    if (result != UDS_OK)
    {
        return result;
    }
    result = wait_for_response(
        client, UDSSendECUReset(&client->upstream, SUB_HARD_RESET));
    if (result == UDS_OK)
    {
        result = validate_subfunction_response(
            client, SID_MCU_RESET_POS, SUB_HARD_RESET, 2U);
    }
    if (result == UDS_OK || result == UDS_ERR_TIMEOUT)
    {
        init_upstream(client);
        client->max_block_length = 0U;
    }
    return result;
}
