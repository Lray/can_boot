#include "uds_server.h"

#include "boot_config.h"
#include "download.h"
#include "isotp.h"
#include "security_access.h"
#include "shared/uds_protocol.h"
#include "uds_read_did.h"
#include "uds_msg.h"

#include <stddef.h>

#define LOG_TAG "uds"
#define LOG_LVL LOG_LVL_DBG
#include "gateway_log.h"

static uint8_t s_session;
static bool s_suppress_positive_response;
static bool s_reset_accepted;
static uint32_t s_now_ms;
static uint32_t s_s3_deadline_ms;
static IsoTpLink *s_transport;

#define UDS_POSITIVE_RESPONSE_BUFFER_SIZE UDS_READ_DID_RESPONSE_MAX_SIZE

typedef enum {
  UDS_S3_INACTIVE = 0,
  UDS_S3_RUNNING,
  UDS_S3_WAITING_FOR_TRANSPORT,
} uds_s3_state_t;

static uds_s3_state_t s_s3_state;

static bool UDS_ResponseInProgress(void)
{
  return (s_transport != NULL) &&
      (s_transport->send_status == ISOTP_SEND_STATUS_INPROGRESS);
}

static void UDS_S3_CompleteResponse(uint32_t now_ms)
{
  if (s_session == SESSION_DEFAULT)
  {
    s_s3_state = UDS_S3_INACTIVE;
    return;
  }

  if (UDS_ResponseInProgress())
  {
    s_s3_state = UDS_S3_WAITING_FOR_TRANSPORT;
    return;
  }

  s_s3_deadline_ms = now_ms + S3_SERVER_DEFAULT;
  s_s3_state = UDS_S3_RUNNING;
}

static void UDS_PollS3(uint32_t now_ms)
{
  if ((s_s3_state == UDS_S3_WAITING_FOR_TRANSPORT) &&
      !UDS_ResponseInProgress())
  {
    UDS_S3_CompleteResponse(now_ms);
  }

  if ((s_s3_state == UDS_S3_RUNNING) &&
      ((int32_t)(now_ms - s_s3_deadline_ms) >= 0))
  {
    s_session = SESSION_DEFAULT;
    s_s3_state = UDS_S3_INACTIVE;
    SecurityAccess_ClearUnlock();
  }
}

static bool UDS_SendPositive(uint8_t request_sid,
                                            const uint8_t *extra_data,
                                            uint16_t extra_length,
                                            bool suppress_positive_response)
{
    uint8_t response[UDS_POSITIVE_RESPONSE_BUFFER_SIZE] = {0};
    uint16_t response_length = 0U;

    if (suppress_positive_response)
    {
        return true;
    }

    response_length = UDS_Msg_BuildPositiveResponseChecked(
        response,
        (uint16_t)sizeof(response),
        request_sid,
        extra_data,
        extra_length);
    if (response_length == 0U)
    {
        return false;
    }

    if ((s_transport == NULL) ||
        (isotp_send(s_transport, response, response_length) != ISOTP_RET_OK))
    {
        GW_LOG_E("positive response send failed sid=0x%02X",
                 (unsigned int)request_sid);
        return false;
    }
    return true;
}

static bool UDS_SendNegative(uint8_t sid, uint8_t nrc)
{
    uint8_t rsp[UDS_NEGATIVE_RESPONSE_LENGTH] = {0};

    (void)UDS_Msg_BuildNegativeResponse(rsp, sid, nrc);
    GW_LOG_W("negative response sid=0x%02X nrc=0x%02X",
          (unsigned int)sid,
          (unsigned int)nrc);
    if ((s_transport == NULL) ||
        (isotp_send(s_transport, rsp, sizeof(rsp)) != ISOTP_RET_OK))
    {
        GW_LOG_E("negative response send failed sid=0x%02X",
                 (unsigned int)sid);
        return false;
    }
    return true;
}

static bool UDS_RequireDownloadSessionAndUnlock(uint8_t sid)
{
    if (s_session != SESSION_PROGRAMMING)
    {
        UDS_SendNegative(sid, NRC_CONDITIONS_NOT_CORRECT);
        return false;
    }

    if (!SecurityAccess_IsUnlocked())
    {
        UDS_SendNegative(sid, NRC_SECURITY_ACCESS_DENIED);
        return false;
    }

    return true;
}

static uint8_t UDS_DownloadResultToNrc(download_result_t result)
{
  switch (result)
  {
    case DOWNLOAD_RESULT_INCORRECT_LENGTH:
      return NRC_INCORRECT_MESSAGE_LENGTH;

    case DOWNLOAD_RESULT_OUT_OF_RANGE:
      return NRC_REQUEST_OUT_OF_RANGE;

    case DOWNLOAD_RESULT_SEQUENCE_ERROR:
      return NRC_REQUEST_SEQUENCE_ERROR;

    case DOWNLOAD_RESULT_WRONG_BLOCK_SEQUENCE:
      return NRC_WRONG_BLOCK_SEQUENCE_COUNTER;

    case DOWNLOAD_RESULT_REJECTED:
      return NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED;

    case DOWNLOAD_RESULT_PROGRAMMING_FAILURE:
      return NRC_GENERAL_PROGRAMMING_FAILURE;

    case DOWNLOAD_RESULT_NOT_READY:
      return NRC_CONDITIONS_NOT_CORRECT;

    default:
      return NRC_GENERAL_PROGRAMMING_FAILURE;
  }
}

static void UDS_RecordSecurityRateLimit(uint32_t now_ms,
                                            security_access_result_t result)
{
  uint8_t failed_attempts = 0U;
  uint32_t remaining_delay_ms = 0U;

  if ((result != SECURITY_ACCESS_RESULT_EXCEEDED_ATTEMPTS) &&
      (result != SECURITY_ACCESS_RESULT_DELAY_ACTIVE))
  {
    return;
  }

  SecurityAccess_GetLockoutStatus(now_ms,
                                      &failed_attempts,
                                      &remaining_delay_ms);
  if (result == SECURITY_ACCESS_RESULT_EXCEEDED_ATTEMPTS)
  {
    GW_LOG_E("security lockout attempts=%u delay_ms=%lu",
          (unsigned int)failed_attempts,
          (unsigned long)remaining_delay_ms);
  }
  else
  {
    GW_LOG_W("security delay active attempts=%u delay_ms=%lu",
          (unsigned int)failed_attempts,
          (unsigned long)remaining_delay_ms);
  }
}

static uint8_t UDS_SecurityAccessResultToNrc(security_access_result_t result)
{
  switch (result)
  {
    case SECURITY_ACCESS_RESULT_INVALID_KEY:
      return NRC_INVALID_KEY;

    case SECURITY_ACCESS_RESULT_EXCEEDED_ATTEMPTS:
      return NRC_EXCEED_NUMBER_OF_ATTEMPTS;

    case SECURITY_ACCESS_RESULT_DELAY_ACTIVE:
      return NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED;

    case SECURITY_ACCESS_RESULT_SEQUENCE_ERROR:
      return NRC_REQUEST_SEQUENCE_ERROR;

    case SECURITY_ACCESS_RESULT_INVALID_ARG:
      return NRC_INCORRECT_MESSAGE_LENGTH;

    case SECURITY_ACCESS_RESULT_ENTROPY_UNAVAILABLE:
      return NRC_CONDITIONS_NOT_CORRECT;

    case SECURITY_ACCESS_RESULT_OK:
    default:
      return NRC_CONDITIONS_NOT_CORRECT;
  }
}

static void UDS_HandleSessionControl(const uint8_t *request, uint16_t length)
{
    uint8_t session;
    uint8_t rsp[5];

    if (length != 2U)
    {
        UDS_SendNegative(SID_DIAGNOSTIC_SESSION_CONTROL,
                             NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    session = request[1] & UDS_SUBFUNCTION_VALUE_MASK;
    if ((session != SESSION_DEFAULT) &&
        (session != SESSION_PROGRAMMING) &&
        (session != SESSION_EXTENDED))
    {
        UDS_SendNegative(SID_DIAGNOSTIC_SESSION_CONTROL,
                             NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    if (s_session != session)
    {
        SecurityAccess_ClearUnlock();
    }
    s_session = session;
    rsp[0] = session;
    rsp[1] = (uint8_t)(P2_SERVER_DEFAULT_MS >> 8);
    rsp[2] = (uint8_t)P2_SERVER_DEFAULT_MS;
    rsp[3] = (uint8_t)(P2_STAR_SERVER_DEFAULT_WIRE >> 8);
    rsp[4] = (uint8_t)P2_STAR_SERVER_DEFAULT_WIRE;
    (void)UDS_SendPositive(
        SID_DIAGNOSTIC_SESSION_CONTROL,
        rsp,
        sizeof(rsp),
        s_suppress_positive_response);
}

static void UDS_HandleTesterPresent(const uint8_t *request, uint16_t length)
{
  uint8_t subfunction;
  uint8_t rsp[1];

  if (length != 2U)
  {
    UDS_SendNegative(SID_TESTER_PRESENT,
                         NRC_INCORRECT_MESSAGE_LENGTH);
    return;
  }

  subfunction = request[1] & UDS_SUBFUNCTION_VALUE_MASK;
  if (subfunction != 0x00U)
  {
    UDS_SendNegative(SID_TESTER_PRESENT,
                         NRC_SUBFUNCTION_NOT_SUPPORTED);
    return;
  }

  rsp[0] = subfunction;
  (void)UDS_SendPositive(
      SID_TESTER_PRESENT, rsp, sizeof(rsp), s_suppress_positive_response);
}

static void UDS_HandleReadDataByIdentifier(
    const uint8_t *request,
    uint16_t length)
{
    uint16_t did = 0U;
    uint8_t rsp[UDS_READ_DID_RESPONSE_MAX_SIZE] = {0};
    uds_read_did_response_t response = {
        rsp,
        UDS_READ_DID_RESPONSE_MAX_SIZE,
        0U,
    };
    uds_read_did_result_t result = UDS_READ_DID_RESULT_BUILD_FAILED;

    if (length != 3U)
    {
        UDS_SendNegative(SID_READ_DATA_BY_IDENTIFIER,
                             NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    did = (uint16_t)(((uint16_t)request[1] << 8) | request[2]);
    result = UDS_ReadDid_Build(did, &response);

    switch (result)
    {
        case UDS_READ_DID_RESULT_OK:
            if (response.length > 0U)
            {
                (void)UDS_SendPositive(
                    SID_READ_DATA_BY_IDENTIFIER,
                    &response.data[1],
                    (uint16_t)(response.length - 1U),
                    s_suppress_positive_response);
            }
            break;

        case UDS_READ_DID_RESULT_OUT_OF_RANGE:
            UDS_SendNegative(
                SID_READ_DATA_BY_IDENTIFIER,
                NRC_REQUEST_OUT_OF_RANGE);
            break;

        case UDS_READ_DID_RESULT_BUILD_FAILED:
            UDS_SendNegative(SID_READ_DATA_BY_IDENTIFIER,
                                 NRC_GENERAL_PROGRAMMING_FAILURE);
            break;

        default:
            UDS_SendNegative(SID_READ_DATA_BY_IDENTIFIER,
                                 NRC_GENERAL_PROGRAMMING_FAILURE);
            break;
    }
}

static void UDS_HandleSecurityAccess(const uint8_t *request, uint16_t length)
{
  uint8_t subfunction;
  uint8_t level;

  if (length < 2U)
  {
    UDS_SendNegative(SID_SECURITY_ACCESS,
                         NRC_INCORRECT_MESSAGE_LENGTH);
    return;
  }

  if (s_session != SESSION_PROGRAMMING)
  {
    UDS_SendNegative(SID_SECURITY_ACCESS,
                         NRC_CONDITIONS_NOT_CORRECT);
    return;
  }

  subfunction = request[1];
  level = (uint8_t)(subfunction & UDS_SUBFUNCTION_VALUE_MASK);
  if (level == SECURITY_ACCESS_LEVEL_PROGRAMMING_SEED)
  {
    uint8_t rsp[1U + SECURITY_ACCESS_SEED_SIZE];
    security_access_result_t result;

    if (length != 2U)
    {
      UDS_SendNegative(SID_SECURITY_ACCESS,
                           NRC_INCORRECT_MESSAGE_LENGTH);
      return;
    }

    rsp[0] = level;
    result = SecurityAccess_RequestSeed(s_now_ms, &rsp[1]);
    if (result != SECURITY_ACCESS_RESULT_OK)
    {
      UDS_RecordSecurityRateLimit(s_now_ms, result);
      UDS_SendNegative(SID_SECURITY_ACCESS,
                           UDS_SecurityAccessResultToNrc(result));
      return;
    }

    GW_LOG_I("security seed issued level=0x%02X bytes=%u",
          (unsigned int)level,
          (unsigned int)SECURITY_ACCESS_SEED_SIZE);
    (void)UDS_SendPositive(
        SID_SECURITY_ACCESS, rsp, sizeof(rsp), s_suppress_positive_response);
    return;
  }

  if (level == SECURITY_ACCESS_LEVEL_PROGRAMMING_KEY)
  {
    uint8_t rsp[] = {SECURITY_ACCESS_LEVEL_PROGRAMMING_KEY};
    security_access_result_t result;

    if (length <= 2U)
    {
      UDS_SendNegative(SID_SECURITY_ACCESS,
                           NRC_INCORRECT_MESSAGE_LENGTH);
      return;
    }

    result = SecurityAccess_SubmitKey(&request[2],
                                          (uint16_t)(length - 2U),
                                          s_now_ms);
    if (result != SECURITY_ACCESS_RESULT_OK)
    {
      uint8_t nrc = UDS_SecurityAccessResultToNrc(result);

      GW_LOG_W("security key rejected nrc=0x%02X result=%u",
            (unsigned int)nrc,
            (unsigned int)result);
      UDS_RecordSecurityRateLimit(s_now_ms, result);
      UDS_SendNegative(SID_SECURITY_ACCESS,
                           nrc);
      return;
    }

    GW_LOG_I("security key accepted level=0x%02X bytes=%u",
          (unsigned int)level,
          (unsigned int)(length - 2U));
    (void)UDS_SendPositive(
        SID_SECURITY_ACCESS, rsp, sizeof(rsp), s_suppress_positive_response);
    return;
  }

  UDS_SendNegative(SID_SECURITY_ACCESS,
                       NRC_SUBFUNCTION_NOT_SUPPORTED);
}

static bool UDS_SendDownloadResponse(uint8_t target_slot,
                                     uint32_t resume_offset)
{
  uint8_t rsp[UDS_REQUEST_DOWNLOAD_RESPONSE_LEN - 1U] = {0};

  rsp[0] = DOWNLOAD_MAX_BLOCK_LEN_FORMAT_ID;
  rsp[UDS_REQUEST_DOWNLOAD_RESPONSE_MAX_BLOCK_OFFSET - 1U] =
      (uint8_t)(DOWNLOAD_MAX_BLOCK_LENGTH >> 8);
  rsp[UDS_REQUEST_DOWNLOAD_RESPONSE_MAX_BLOCK_OFFSET] =
      (uint8_t)DOWNLOAD_MAX_BLOCK_LENGTH;
  rsp[UDS_REQUEST_DOWNLOAD_RESPONSE_TARGET_SLOT_OFFSET - 1U] = target_slot;
  UDS_Msg_WriteBe32(
      &rsp[UDS_REQUEST_DOWNLOAD_RESPONSE_RESUME_OFFSET - 1U],
      resume_offset);
  return UDS_SendPositive(SID_REQUEST_DOWNLOAD,
                                         rsp,
                                         sizeof(rsp),
                                         false);
}

static void UDS_HandleRequestDownload(const uint8_t *request, uint16_t length)
{
    uint32_t address = 0U;
    uint32_t size = 0U;
    uint32_t resume_offset = 0U;
    download_result_t result = DOWNLOAD_RESULT_SEQUENCE_ERROR;
    uint8_t target_slot = SLOT_INVALID;

    if (length != UDS_REQUEST_DOWNLOAD_REQUEST_LEN)
    {
        UDS_SendNegative(SID_REQUEST_DOWNLOAD,
                             NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    if (!UDS_RequireDownloadSessionAndUnlock(
            SID_REQUEST_DOWNLOAD))
    {
        return;
    }

    if ((request[1] != DOWNLOAD_DATA_FORMAT_ID) ||
        (request[2] != DOWNLOAD_ADDR_LEN_FORMAT_ID))
    {
        UDS_SendNegative(SID_REQUEST_DOWNLOAD,
                             NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    address = UDS_Msg_ReadBe32(&request[3]);
    size = UDS_Msg_ReadBe32(&request[7]);
    if (address != DOWNLOAD_MEMORY_ADDRESS)
    {
        UDS_SendNegative(SID_REQUEST_DOWNLOAD, NRC_REQUEST_OUT_OF_RANGE);
        return;
    }

    result = Download_Begin(
        &request[UDS_REQUEST_DOWNLOAD_PAYLOAD_ID_OFFSET],
        size,
        &target_slot,
        &resume_offset);
    if (result != DOWNLOAD_RESULT_OK)
    {
        UDS_SendNegative(
            SID_REQUEST_DOWNLOAD,
            UDS_DownloadResultToNrc(result));
        return;
    }

    (void)UDS_SendDownloadResponse(target_slot, resume_offset);
}

static void UDS_HandleRoutineControl(const uint8_t *request, uint16_t length)
{
  uint8_t subfunction = 0U;
  uint8_t rsp[UDS_PREPARE_DOWNLOAD_ROUTINE_RESULT_LEN - 1U] = {0};
  uint16_t routine_id = 0U;

  if (length < UDS_ROUTINE_CONTROL_REQUEST_LEN)
  {
    UDS_SendNegative(SID_ROUTINE_CONTROL, NRC_INCORRECT_MESSAGE_LENGTH);
    return;
  }

  subfunction = request[1] & UDS_SUBFUNCTION_VALUE_MASK;
  routine_id = (uint16_t)(((uint16_t)request[2] << 8) | request[3]);
  if (routine_id != ROUTINE_ID_PREPARE_DOWNLOAD)
  {
    UDS_SendNegative(SID_ROUTINE_CONTROL, NRC_REQUEST_OUT_OF_RANGE);
    return;
  }

  if (!UDS_RequireDownloadSessionAndUnlock(SID_ROUTINE_CONTROL))
  {
    return;
  }

  rsp[0] = subfunction;
  rsp[1] = request[2];
  rsp[2] = request[3];
  if (subfunction == ROUTINE_CONTROL_START)
  {
    download_result_t result = DOWNLOAD_RESULT_SEQUENCE_ERROR;

    if (length != UDS_PREPARE_DOWNLOAD_ROUTINE_REQUEST_LEN)
    {
      UDS_SendNegative(SID_ROUTINE_CONTROL, NRC_INCORRECT_MESSAGE_LENGTH);
      return;
    }

    result = Download_Prepare(
        &request[UDS_PREPARE_DOWNLOAD_ROUTINE_PAYLOAD_ID_OFFSET],
        UDS_Msg_ReadBe32(&request[UDS_PREPARE_DOWNLOAD_ROUTINE_SIZE_OFFSET]));
    if (result != DOWNLOAD_RESULT_OK)
    {
      UDS_SendNegative(SID_ROUTINE_CONTROL, UDS_DownloadResultToNrc(result));
      return;
    }

    (void)UDS_SendPositive(SID_ROUTINE_CONTROL,
                           rsp,
                           UDS_ROUTINE_CONTROL_REQUEST_LEN - 1U,
                           s_suppress_positive_response);
    return;
  }

  if (subfunction != ROUTINE_CONTROL_REQUEST_RESULTS)
  {
    UDS_SendNegative(SID_ROUTINE_CONTROL, NRC_SUBFUNCTION_NOT_SUPPORTED);
    return;
  }
  if (length != UDS_ROUTINE_CONTROL_REQUEST_LEN)
  {
    UDS_SendNegative(SID_ROUTINE_CONTROL, NRC_INCORRECT_MESSAGE_LENGTH);
    return;
  }

  switch (Download_GetPreparationStatus())
  {
    case DOWNLOAD_PREPARATION_PENDING:
      rsp[3] = ROUTINE_PREPARE_DOWNLOAD_STATUS_PENDING;
      break;

    case DOWNLOAD_PREPARATION_READY:
      rsp[3] = ROUTINE_PREPARE_DOWNLOAD_STATUS_READY;
      break;

    case DOWNLOAD_PREPARATION_FAILED:
      UDS_SendNegative(SID_ROUTINE_CONTROL, NRC_GENERAL_PROGRAMMING_FAILURE);
      return;

    case DOWNLOAD_PREPARATION_IDLE:
    default:
      UDS_SendNegative(SID_ROUTINE_CONTROL, NRC_REQUEST_SEQUENCE_ERROR);
      return;
  }

  (void)UDS_SendPositive(SID_ROUTINE_CONTROL,
                         rsp,
                         sizeof(rsp),
                         s_suppress_positive_response);
}

static void UDS_HandleTransferData(const uint8_t *request, uint16_t length)
{
  download_result_t result = DOWNLOAD_RESULT_SEQUENCE_ERROR;
  uint8_t rsp[1] = {0};

  if (length < 2U)
  {
    UDS_SendNegative(SID_TRANSFER_DATA,
                         NRC_INCORRECT_MESSAGE_LENGTH);
    return;
  }

  if (!UDS_RequireDownloadSessionAndUnlock(SID_TRANSFER_DATA))
  {
    return;
  }

  result = Download_Transfer(request[1], &request[2], (uint16_t)(length - 2U));
  if (result != DOWNLOAD_RESULT_OK)
  {
    uint8_t nrc = UDS_DownloadResultToNrc(result);

    GW_LOG_W("transfer failed nrc=0x%02X bsc=0x%02X",
          (unsigned int)nrc,
          (unsigned int)request[1]);
    UDS_SendNegative(SID_TRANSFER_DATA,
                         nrc);
    return;
  }

  GW_LOG_D("transfer accepted bsc=0x%02X", (unsigned int)request[1]);
  rsp[0] = request[1];
  (void)UDS_SendPositive(
      SID_TRANSFER_DATA, rsp, sizeof(rsp), s_suppress_positive_response);
}

static void UDS_HandleRequestTransferExit(const uint8_t *request, uint16_t length)
{
  download_result_t result = DOWNLOAD_RESULT_SEQUENCE_ERROR;
  (void)request;

  if (length != 1U)
  {
    UDS_SendNegative(SID_REQUEST_TRANSFER_EXIT,
                         NRC_INCORRECT_MESSAGE_LENGTH);
    return;
  }

  if (!UDS_RequireDownloadSessionAndUnlock(SID_REQUEST_TRANSFER_EXIT))
  {
    return;
  }

  result = Download_Exit();
  if (result != DOWNLOAD_RESULT_OK)
  {
    UDS_SendNegative(SID_REQUEST_TRANSFER_EXIT,
                         UDS_DownloadResultToNrc(result));
    return;
  }

  GW_LOG_I("transfer exit accepted");

  (void)UDS_SendPositive(
      SID_REQUEST_TRANSFER_EXIT,
      NULL,
      0U,
      s_suppress_positive_response);
}

static void UDS_HandleEcuReset(const uint8_t *request, uint16_t length)
{
    uint8_t rsp[1];

    if (length != 2U)
    {
        UDS_SendNegative(SID_ECU_RESET,
                             NRC_INCORRECT_MESSAGE_LENGTH);
        return;
    }

    if ((request[1] & UDS_SUBFUNCTION_VALUE_MASK) != SUB_HARD_RESET)
    {
        UDS_SendNegative(SID_ECU_RESET,
                             NRC_SUBFUNCTION_NOT_SUPPORTED);
        return;
    }

    if (!UDS_RequireDownloadSessionAndUnlock(
            SID_ECU_RESET))
    {
        return;
    }

    rsp[0] = SUB_HARD_RESET;
    if (UDS_SendPositive(
            SID_ECU_RESET, rsp, sizeof(rsp), s_suppress_positive_response))
    {
        GW_LOG_I("reset requested subfunction=0x%02X",
              (unsigned int)SUB_HARD_RESET);
        s_reset_accepted = true;
    }
}

void UDS_Init(IsoTpLink *transport)
{
    s_transport = transport;
    s_session = SESSION_DEFAULT;
    s_reset_accepted = false;
    s_now_ms = 0U;
    s_s3_deadline_ms = 0U;
    s_s3_state = UDS_S3_INACTIVE;
    s_suppress_positive_response = false;
    SecurityAccess_Init();
    Download_Init();
}

void UDS_Poll(uint32_t now_ms)
{
  s_now_ms = now_ms;
  UDS_PollS3(now_ms);
  SecurityAccess_Poll(now_ms);
}

void UDS_Dispatch(const uint8_t *request, uint16_t length)
{
  uint8_t sid;

  if ((request == 0) || (length == 0U))
  {
    return;
  }

  sid = request[0];
  UDS_PollS3(s_now_ms);
  if ((s_s3_state == UDS_S3_WAITING_FOR_TRANSPORT) &&
      UDS_ResponseInProgress())
  {
    return;
  }
  s_s3_state = UDS_S3_INACTIVE;
  s_suppress_positive_response =
      (length >= 2U) &&
       ((sid == SID_DIAGNOSTIC_SESSION_CONTROL) ||
        (sid == SID_ECU_RESET) ||
        (sid == SID_SECURITY_ACCESS) ||
        (sid == SID_ROUTINE_CONTROL) ||
        (sid == SID_TESTER_PRESENT)) &&
      ((request[1] & UDS_SUBFUNCTION_SUPPRESS_POSITIVE_RESPONSE_MASK) != 0U);

  switch (sid)
  {
    case SID_DIAGNOSTIC_SESSION_CONTROL:
      UDS_HandleSessionControl(request, length);
      break;

    case SID_TESTER_PRESENT:
      UDS_HandleTesterPresent(request, length);
      break;

    case SID_READ_DATA_BY_IDENTIFIER:
      UDS_HandleReadDataByIdentifier(request, length);
      break;

    case SID_SECURITY_ACCESS:
      UDS_HandleSecurityAccess(request, length);
      break;

    case SID_ROUTINE_CONTROL:
      UDS_HandleRoutineControl(request, length);
      break;

    case SID_REQUEST_DOWNLOAD:
      UDS_HandleRequestDownload(request, length);
      break;

    case SID_TRANSFER_DATA:
      UDS_HandleTransferData(request, length);
      break;

    case SID_REQUEST_TRANSFER_EXIT:
      UDS_HandleRequestTransferExit(request, length);
      break;

    case SID_ECU_RESET:
      UDS_HandleEcuReset(request, length);
      break;

    default:
      UDS_SendNegative(sid, NRC_SERVICE_NOT_SUPPORTED);
      break;
  }

  UDS_S3_CompleteResponse(s_now_ms);

  s_suppress_positive_response = false;
}

bool UDS_ConsumeAcceptedReset(void)
{
    bool accepted = s_reset_accepted;

    s_reset_accepted = false;
    return accepted;
}
