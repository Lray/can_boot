#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "download.h"
#include "security_access.h"
#include "shared/uds_protocol.h"
#include "uds_read_did.h"
#include "uds_server.h"

static uint8_t s_response[16];
static uint16_t s_response_length;
static bool s_pending;
static bool s_async_send;
static bool s_unlocked;
static unsigned int s_abort_count;
static download_preparation_status_t s_preparation_status;

static bool send_response(const uint8_t *data, uint16_t length)
{
    assert(length <= sizeof(s_response));
    memcpy(s_response, data, length);
    s_response_length = length;
    s_pending = s_async_send;
    return true;
}

static bool response_pending(void)
{
    return s_pending;
}

static void dispatch_at(uint32_t now, const uint8_t *request, uint16_t length)
{
    UDS_Poll(now);
    s_response_length = 0U;
    UDS_Dispatch(request, length);
}

void SecurityAccess_Init(void) { s_unlocked = false; }
void SecurityAccess_ClearUnlock(void) { s_unlocked = false; }
void SecurityAccess_Poll(uint32_t now_ms) { (void)now_ms; }
bool SecurityAccess_IsUnlocked(void) { return s_unlocked; }
security_access_result_t SecurityAccess_RequestSeed(uint32_t now_ms,
                                                    uint8_t seed[SECURITY_ACCESS_SEED_SIZE])
{
    (void)now_ms;
    memset(seed, 1, SECURITY_ACCESS_SEED_SIZE);
    return SECURITY_ACCESS_RESULT_OK;
}
security_access_result_t SecurityAccess_SubmitKey(const uint8_t *key,
                                                   uint16_t length,
                                                   uint32_t now_ms)
{
    (void)key;
    (void)length;
    (void)now_ms;
    s_unlocked = true;
    return SECURITY_ACCESS_RESULT_OK;
}
void SecurityAccess_GetLockoutStatus(uint32_t now_ms,
                                     uint8_t *failed_attempts,
                                     uint32_t *remaining_delay_ms)
{
    (void)now_ms;
    (void)failed_attempts;
    (void)remaining_delay_ms;
}

void Download_Init(void)
{
    s_abort_count = 0U;
    s_preparation_status = DOWNLOAD_PREPARATION_READY;
}
void Download_Abort(void) { s_abort_count++; }
download_result_t Download_Prepare(void) { return DOWNLOAD_RESULT_OK; }
void Download_Poll(void) {}
download_preparation_status_t Download_GetPreparationStatus(void)
{
    return s_preparation_status;
}
download_result_t Download_Begin(uint32_t image_size)
{
    return image_size == 0U ? DOWNLOAD_RESULT_OUT_OF_RANGE : DOWNLOAD_RESULT_OK;
}
download_result_t Download_Transfer(uint8_t bsc, const uint8_t *payload,
                                    uint16_t length)
{
    (void)bsc;
    (void)payload;
    return length == 0U ? DOWNLOAD_RESULT_INCORRECT_LENGTH : DOWNLOAD_RESULT_OK;
}
download_result_t Download_Exit(void) { return DOWNLOAD_RESULT_OK; }

uds_read_did_result_t UDS_ReadDid_Build(uint16_t did,
                                        uds_read_did_response_t *response)
{
    if (did != DID_BOOT_VERSION)
    {
        return UDS_READ_DID_RESULT_OUT_OF_RANGE;
    }
    response->data[0] = SID_READ_DATA_BY_IDENTIFIER_POS;
    response->data[1] = (uint8_t)(did >> 8);
    response->data[2] = (uint8_t)did;
    response->data[3] = 0x42U;
    response->length = 4U;
    return UDS_READ_DID_RESULT_OK;
}

static void test_normal_requests_restart_s3(void)
{
    static const uint8_t session[] = {0x10U, 0x02U};
    static const uint8_t seed[] = {0x27U, 0x01U};
    static const uint8_t key[] = {0x27U, 0x02U, 0x55U};
    static const uint8_t erase[] = {0x31U, 0x01U, 0xFFU, 0x00U};
    static const uint8_t erase_results[] = {0x31U, 0x03U, 0xFFU, 0x00U};
    static const uint8_t download[] = {0x34U, 0x00U, 0x44U, 0, 0, 0, 0, 0, 0, 1, 0};
    uint8_t download_with_sha[43] = {0};
    static const uint8_t transfer[] = {0x36U, 0x01U, 0x42U};
    static const uint8_t exit_request[] = {0x37U};
    static const uint8_t reset[] = {0x11U, 0x01U};
    static const uint8_t did[] = {0x22U, 0xF1U, 0xF0U};
    static const uint8_t old_did[] = {0x22U, 0xF1U, 0x80U};

    UDS_Init(&(uds_transport_t){send_response, response_pending});
    dispatch_at(0U, session, sizeof(session));
    assert(s_response[0] == 0x50U);
    dispatch_at(4000U, seed, sizeof(seed));
    assert(s_response[0] == 0x67U);
    dispatch_at(8000U, key, sizeof(key));
    assert(s_unlocked);
    dispatch_at(12000U, erase, sizeof(erase));
    assert(s_response[0] == 0x71U);
    s_preparation_status = DOWNLOAD_PREPARATION_PENDING;
    dispatch_at(12001U, erase_results, sizeof(erase_results));
    assert(s_response[2] == NRC_REQUEST_SEQUENCE_ERROR);
    s_preparation_status = DOWNLOAD_PREPARATION_FAILED;
    dispatch_at(12002U, erase_results, sizeof(erase_results));
    assert(s_response[2] == NRC_GENERAL_PROGRAMMING_FAILURE);
    s_preparation_status = DOWNLOAD_PREPARATION_READY;
    dispatch_at(12003U, erase_results, sizeof(erase_results));
    assert(s_response_length == UDS_ERASE_MEMORY_RESULT_LEN);
    assert(s_response[0] == 0x71U);
    dispatch_at(16000U, download, sizeof(download));
    assert(s_response_length == 4U);
    assert(memcmp(s_response, (uint8_t[]){0x74U, 0x20U, 0x01U, 0x02U}, 4U) == 0);
    memcpy(download_with_sha, download, sizeof(download));
    dispatch_at(16001U, download_with_sha, sizeof(download_with_sha));
    assert(s_response[2] == NRC_INCORRECT_MESSAGE_LENGTH);
    dispatch_at(20000U, transfer, sizeof(transfer));
    assert(s_response[0] == 0x76U);
    dispatch_at(24000U, did, sizeof(did));
    assert(s_response[0] == 0x62U);
    dispatch_at(28000U, old_did, sizeof(old_did));
    assert(s_response[0] == 0x7FU && s_response[2] == NRC_REQUEST_OUT_OF_RANGE);
    UDS_Poll(33099U);
    assert(s_unlocked);
    UDS_Poll(33100U);
    assert(!s_unlocked && s_abort_count == 1U);
    dispatch_at(33101U, download, sizeof(download));
    assert(s_response[2] == NRC_SERVICE_NOT_SUPPORTED_IN_ACTIVE_SESSION);
    dispatch_at(33102U, seed, sizeof(seed));
    assert(s_response[2] == NRC_SUBFUNCTION_NOT_SUPPORTED_IN_ACTIVE_SESSION);
    dispatch_at(33103U, erase, sizeof(erase));
    assert(s_response[2] == NRC_SUBFUNCTION_NOT_SUPPORTED_IN_ACTIVE_SESSION);
    dispatch_at(33104U, reset, sizeof(reset));
    assert(s_response[2] == NRC_SUBFUNCTION_NOT_SUPPORTED_IN_ACTIVE_SESSION);
    dispatch_at(33105U, transfer, sizeof(transfer));
    assert(s_response[2] == NRC_SERVICE_NOT_SUPPORTED_IN_ACTIVE_SESSION);
    dispatch_at(33106U, exit_request, sizeof(exit_request));
    assert(s_response[2] == NRC_SERVICE_NOT_SUPPORTED_IN_ACTIVE_SESSION);
}

static void test_async_and_suppressed_responses(void)
{
    static const uint8_t session[] = {0x10U, 0x02U};
    static const uint8_t did[] = {0x22U, 0xF1U, 0xF0U};
    static const uint8_t unsupported[] = {0x99U};
    static const uint8_t suppressed[] = {0x3EU, 0x80U};
    static const uint8_t download[] = {0x34U, 0x00U, 0x44U, 0, 0, 0, 0, 0, 0, 1, 0};

    UDS_Init(&(uds_transport_t){send_response, response_pending});
    dispatch_at(0U, session, sizeof(session));
    s_async_send = true;
    dispatch_at(4000U, did, sizeof(did));
    UDS_Poll(20000U);
    assert(s_abort_count == 0U);
    s_pending = false;
    s_async_send = false;
    UDS_Poll(20001U);
    dispatch_at(24000U, unsupported, sizeof(unsupported));
    assert(s_response[2] == NRC_SERVICE_NOT_SUPPORTED);
    dispatch_at(28000U, suppressed, sizeof(suppressed));
    assert(s_response_length == 0U);
    dispatch_at(32000U, download, sizeof(download));
    assert(s_response[2] == NRC_SECURITY_ACCESS_DENIED);
    UDS_Poll(37100U);
    assert(s_abort_count == 1U);
}

int main(void)
{
    test_normal_requests_restart_s3();
    test_async_and_suppressed_responses();
    return 0;
}
