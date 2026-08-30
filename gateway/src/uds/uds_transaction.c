#include "uds_transaction.h"

#include "profile.h"
#include "util.h"

#define UDS_TRANSACTION_PENDING 1
#define ISOTP_SINGLE_FRAME_PAYLOAD 7u
#define ISOTP_FIRST_FRAME_PAYLOAD 6u
#define ISOTP_CONSECUTIVE_FRAME_PAYLOAD 7u

/* 首响应等待：P2ServerMax 加上多帧请求的本地 ISO-TP 发送耗时。 */
static uint32_t first_response_timeout_ms(const UdsClient *client, size_t request_len)
{
    uint32_t timeout_ms = client->p2_server_max_ms;

    if (request_len > ISOTP_SINGLE_FRAME_PAYLOAD)
    {
        size_t remaining = request_len - ISOTP_FIRST_FRAME_PAYLOAD;
        size_t cf_count = (remaining + ISOTP_CONSECUTIVE_FRAME_PAYLOAD - 1u) /
                          ISOTP_CONSECUTIVE_FRAME_PAYLOAD;

        timeout_ms += (uint32_t)(cf_count * ISOTP_STMIN_MS) + UDS_ISOTP_TX_MARGIN_MS;
    }

    return timeout_ms;
}

/* 本次 recv 等待 = min(wanted_ms, UDS_MAX_TRANSACTION_MS-elapsed)；
 * 返回 false 表示总预算（含 ResponsePending 重试）已耗尽。 */
static bool clamp_receive_timeout(uint64_t elapsed_ms, uint32_t wanted_ms,
                                  uint32_t *receive_timeout_ms)
{
    uint64_t remaining_ms = 0u;

    if (elapsed_ms >= UDS_MAX_TRANSACTION_MS)
    {
        return false;
    }
    remaining_ms = UDS_MAX_TRANSACTION_MS - elapsed_ms;
    *receive_timeout_ms = wanted_ms;
    if (remaining_ms < (uint64_t)*receive_timeout_ms)
    {
        *receive_timeout_ms = (uint32_t)remaining_ms;
    }
    if (*receive_timeout_ms == 0u)
    {
        *receive_timeout_ms = 1u;
    }
    return true;
}

static int transaction_receive(UdsClient *client, uint8_t *response,
                               size_t response_cap, uint32_t receive_timeout_ms,
                               size_t *rx_len_out)
{
    int rc = 0;

    rc = client->transport->recv(client->transport_ctx, response, response_cap,
                                 rx_len_out, receive_timeout_ms);
    return rc == 0 ? 0 : UDS_ERR_TIMEOUT;
}

/* 返回 UDS_TRANSACTION_PENDING 表示继续接收，0 表示事务完成，否则为 UDS_ERR_*。 */
static int transaction_classify(UdsClient *client, const uint8_t *request,
                                const uint8_t *response, size_t rx_len)
{
    if (rx_len == 0u)
    {
        return UDS_ERR_MALFORMED_RESPONSE;
    }
    if (response[0] == NEGATIVE_RESPONSE_SID)
    {
        if (rx_len != UDS_NEGATIVE_RESPONSE_LENGTH)
        {
            return UDS_ERR_MALFORMED_RESPONSE;
        }
        if (response[1] != request[0])
        {
            return UDS_ERR_UNEXPECTED_RESPONSE;
        }
        if (response[2] == NRC_RESPONSE_PENDING)
        {
            return UDS_TRANSACTION_PENDING;
        }
        client->last_nrc = response[2];
        return UDS_ERR_NEGATIVE_RESPONSE;
    }
    if (response[0] != (request[0] | SID_POSITIVE_RESPONSE_MASK))
    {
        return UDS_ERR_UNEXPECTED_RESPONSE;
    }
    return 0;
}

int uds_transaction_request(UdsClient *client, const uint8_t *request, size_t request_len,
                            uint8_t *response, size_t response_cap, size_t *response_len)
{
    uint32_t timeout_ms = 0u;
    uint64_t transaction_start_ms = 0u;
    uint8_t response_pending_count = 0u;

    if (!uds_client_is_ready(client) || request == NULL || request_len == 0u ||
        response == NULL || response_cap == 0u || response_len == NULL)
    {
        return UDS_ERR_INVALID_ARG;
    }
    *response_len = 0u;
    client->last_nrc = 0u;
    timeout_ms = first_response_timeout_ms(client, request_len);
    transaction_start_ms = util_monotonic_ms();

    if (client->transport->send(client->transport_ctx, request, request_len) != 0)
    {
        return UDS_ERR_TRANSPORT;
    }

    for (;;)
    {
        uint32_t receive_timeout_ms = 0u;
        size_t rx_len = 0u;
        int classify_result = 0;

        if (!clamp_receive_timeout(
                util_monotonic_ms() - transaction_start_ms, timeout_ms,
                &receive_timeout_ms))
        {
            return UDS_ERR_TIMEOUT;
        }
        if (transaction_receive(client, response, response_cap,
                                receive_timeout_ms, &rx_len) != 0)
        {
            *response_len = 0u;
            return UDS_ERR_TIMEOUT;
        }
        *response_len = rx_len;
        if (rx_len > response_cap)
        {
            return UDS_ERR_BUFFER_TOO_SMALL;
        }
        classify_result = transaction_classify(client, request, response, rx_len);
        if (classify_result != UDS_TRANSACTION_PENDING)
        {
            return classify_result;
        }
        if (response_pending_count >= UDS_RESPONSE_PENDING_MAX_COUNT)
        {
            return UDS_ERR_RESPONSE_PENDING_LIMIT;
        }
        response_pending_count++;
        *response_len = 0u;
        timeout_ms = client->p2_star_server_max_ms;
    }
}