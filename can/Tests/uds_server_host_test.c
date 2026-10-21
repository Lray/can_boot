#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "download.h"
#include "security_access.h"
#include "shared/uds_protocol.h"
#include "uds_read_did.h"
#include "uds_server.h"

static uint8_t s_response[32];
static uint16_t s_response_length;
static bool s_unlocked;
static bool s_download_active;
static unsigned int s_abort_count;
static uint8_t s_last_bsc;
static download_preparation_status_t s_preparation_status;

static bool send_response(const uint8_t *data, uint16_t length)
{
    assert(length <= sizeof(s_response));
    memcpy(s_response, data, length);
    s_response_length = length;
    return true;
}

static bool response_pending(void)
{
    return false;
}

static void transact(uint32_t now, const uint8_t *request, uint16_t length)
{
    s_response_length = 0U;
    UDS_Dispatch(request, length);
    UDS_Poll(now);
    UDS_Poll(now + P2_SERVER_DEFAULT_MS + 1U);
    assert(s_response_length != 0U);
}

void SecurityAccess_Init(void) { s_unlocked = false; }
void SecurityAccess_ClearUnlock(void) { s_unlocked = false; }
bool SecurityAccess_IsUnlocked(void) { return s_unlocked; }
security_access_result_t SecurityAccess_RequestSeed(
    uint8_t seed[SECURITY_ACCESS_SEED_SIZE])
{
    memset(seed, 1, SECURITY_ACCESS_SEED_SIZE);
    return SECURITY_ACCESS_RESULT_OK;
}
security_access_result_t SecurityAccess_SubmitKey(
    const uint8_t *key, uint16_t length)
{
    (void)key;
    (void)length;
    s_unlocked = true;
    return SECURITY_ACCESS_RESULT_OK;
}

void Download_Init(void)
{
    s_abort_count = 0U;
    s_download_active = false;
    s_last_bsc = 0U;
    s_preparation_status = DOWNLOAD_PREPARATION_READY;
}
void Download_Abort(void)
{
    s_abort_count++;
    s_download_active = false;
}
download_result_t Download_Prepare(void) { return DOWNLOAD_RESULT_OK; }
void Download_Poll(void) {}
download_preparation_status_t Download_GetPreparationStatus(void)
{
    return s_preparation_status;
}
download_result_t Download_Begin(uint32_t image_size)
{
    s_download_active = image_size != 0U;
    return image_size == 0U ? DOWNLOAD_RESULT_OUT_OF_RANGE : DOWNLOAD_RESULT_OK;
}
download_result_t Download_Transfer(uint8_t bsc, const uint8_t *payload,
                                    uint16_t length)
{
    (void)payload;
    s_last_bsc = bsc;
    return length == 0U ? DOWNLOAD_RESULT_INCORRECT_LENGTH : DOWNLOAD_RESULT_OK;
}
download_result_t Download_Exit(void) { return DOWNLOAD_RESULT_OK; }

uds_read_did_result_t UDS_ReadDid_BuildPayload(
    uint16_t did, uds_read_did_payload_t *payload)
{
    if (did != DID_BOOT_VERSION)
    {
        return UDS_READ_DID_RESULT_OUT_OF_RANGE;
    }
    payload->data[0] = 0x42U;
    payload->length = 1U;
    return UDS_READ_DID_RESULT_OK;
}

static void enter_programming_and_unlock(void)
{
    static const uint8_t session[] = {0x10U, 0x02U};
    static const uint8_t seed[] = {0x27U, 0x01U};
    static const uint8_t key[] = {0x27U, 0x02U, 0x55U};

    transact(1001U, session, sizeof(session));
    assert(memcmp(s_response,
                  (uint8_t[]){0x50U, 0x02U, 0x00U, 0x32U, 0x01U, 0xF4U},
                  6U) == 0);
    transact(1100U, seed, sizeof(seed));
    assert(s_response_length == 6U && s_response[0] == 0x67U);
    transact(1200U, key, sizeof(key));
    assert(memcmp(s_response, (uint8_t[]){0x67U, 0x02U}, 2U) == 0);
    assert(s_unlocked);
}

static void test_upstream_session_security_and_did(void)
{
    static const uint8_t seed[] = {0x27U, 0x01U};
    static const uint8_t did[] = {0x22U, 0xF1U, 0xF0U};
    static const uint8_t unsupported_did[] = {0x22U, 0xF1U, 0x80U};

    UDS_Init(&(uds_transport_t){send_response, response_pending});
    transact(100U, seed, sizeof(seed));
    assert(memcmp(s_response, (uint8_t[]){0x7FU, 0x27U, 0x37U}, 3U) == 0);

    enter_programming_and_unlock();
    transact(1300U, seed, sizeof(seed));
    assert(memcmp(s_response, (uint8_t[]){0x67U, 0x01U, 0x00U, 0x00U}, 4U) == 0);
    transact(1400U, did, sizeof(did));
    assert(memcmp(s_response, (uint8_t[]){0x62U, 0xF1U, 0xF0U, 0x42U}, 4U) == 0);
    transact(1500U, unsupported_did, sizeof(unsupported_did));
    assert(memcmp(s_response, (uint8_t[]){0x7FU, 0x22U, 0x31U}, 3U) == 0);
}

static void test_upstream_download_sequence(void)
{
    static const uint8_t erase[] = {0x31U, 0x01U, 0xFFU, 0x00U};
    static const uint8_t erase_results[] = {0x31U, 0x03U, 0xFFU, 0x00U};
    static const uint8_t download[] = {
        0x34U, 0x00U, 0x44U, 0, 0, 0, 0, 0, 0, 0, 2,
    };
    static const uint8_t block[] = {0x36U, 0x01U, 0xAAU, 0xBBU};
    static const uint8_t duplicate_bsc[] = {0x36U, 0x01U, 0xAAU};
    static const uint8_t exit_request[] = {0x37U};

    UDS_Init(&(uds_transport_t){send_response, response_pending});
    enter_programming_and_unlock();
    transact(1300U, erase, sizeof(erase));
    assert(memcmp(s_response, (uint8_t[]){0x71U, 0x01U, 0xFFU, 0x00U}, 4U) == 0);
    transact(1400U, erase_results, sizeof(erase_results));
    assert(s_response_length == UDS_ERASE_MEMORY_RESULT_LEN);
    transact(1500U, download, sizeof(download));
    assert(memcmp(s_response, (uint8_t[]){0x74U, 0x20U, 0x01U, 0x02U}, 4U) == 0);
    transact(1600U, block, sizeof(block));
    assert(memcmp(s_response, (uint8_t[]){0x76U, 0x01U}, 2U) == 0);
    assert(s_last_bsc == 0x01U);
    transact(1700U, duplicate_bsc, sizeof(duplicate_bsc));
    assert(memcmp(s_response, (uint8_t[]){0x7FU, 0x36U, 0x24U}, 3U) == 0);
    transact(1800U, exit_request, sizeof(exit_request));
    assert(memcmp(s_response, (uint8_t[]){0x7FU, 0x37U, 0x70U}, 3U) == 0);

    transact(1900U, erase, sizeof(erase));
    transact(2000U, download, sizeof(download));
    transact(2100U, block, sizeof(block));
    transact(2200U, exit_request, sizeof(exit_request));
    assert(s_response_length == 1U && s_response[0] == 0x77U);
}

static void test_only_session_control_and_tester_present_refresh_s3(void)
{
    static const uint8_t did[] = {0x22U, 0xF1U, 0xF0U};
    static const uint8_t download[] = {
        0x34U, 0x00U, 0x44U, 0, 0, 0, 0, 0, 0, 0, 1,
    };

    UDS_Init(&(uds_transport_t){send_response, response_pending});
    enter_programming_and_unlock();
    transact(5000U, did, sizeof(did));
    UDS_Poll(6102U);
    assert(!s_unlocked);
    transact(6200U, download, sizeof(download));
    assert(memcmp(s_response, (uint8_t[]){0x7FU, 0x34U, 0x7FU}, 3U) == 0);
}

int main(void)
{
    test_upstream_session_security_and_did();
    test_upstream_download_sequence();
    test_only_session_control_and_tester_present_refresh_s3();
    return 0;
}
