#include "uds_server.h"

#include "download.h"
#include "security_access.h"
#include "shared/uds_protocol.h"
#include "uds_read_did.h"

#include <stddef.h>
#include <stdint.h>

#define LOG_TAG "uds"
#define LOG_LVL LOG_LVL_DBG
#include "gateway_log.h"

static UDSServer_t s_server;
static bool s_reset_accepted;
static uint32_t s_now_ms;

uint32_t UDSMillis(void)
{
    return s_now_ms;
}

static UDSErr_t require_programming_and_unlock(
    const UDSServer_t *server,
    bool subfunction_service)
{
    if (server->sessionType != UDS_LEV_DS_PRGS)
    {
        return subfunction_service
                   ? UDS_NRC_SubFunctionNotSupportedInActiveSession
                   : UDS_NRC_ServiceNotSupportedInActiveSession;
    }
    return SecurityAccess_IsUnlocked()
               ? UDS_PositiveResponse
               : UDS_NRC_SecurityAccessDenied;
}

static UDSErr_t download_result_to_nrc(download_result_t result)
{
    switch (result)
    {
        case DOWNLOAD_RESULT_INCORRECT_LENGTH:
            return UDS_NRC_IncorrectMessageLengthOrInvalidFormat;
        case DOWNLOAD_RESULT_OUT_OF_RANGE:
            return UDS_NRC_RequestOutOfRange;
        case DOWNLOAD_RESULT_TRANSFER_SUSPENDED:
            return UDS_NRC_TransferDataSuspended;
        case DOWNLOAD_RESULT_SEQUENCE_ERROR:
            return UDS_NRC_RequestSequenceError;
        case DOWNLOAD_RESULT_WRONG_BLOCK_SEQUENCE:
            return UDS_NRC_WrongBlockSequenceCounter;
        case DOWNLOAD_RESULT_REJECTED:
            return UDS_NRC_UploadDownloadNotAccepted;
        case DOWNLOAD_RESULT_PROGRAMMING_FAILURE:
            return UDS_NRC_GeneralProgrammingFailure;
        case DOWNLOAD_RESULT_NOT_READY:
            return UDS_NRC_ConditionsNotCorrect;
        case DOWNLOAD_RESULT_OK:
        default:
            return UDS_NRC_GeneralProgrammingFailure;
    }
}

static UDSErr_t security_result_to_nrc(security_access_result_t result)
{
    switch (result)
    {
        case SECURITY_ACCESS_RESULT_INVALID_KEY:
            return UDS_NRC_InvalidKey;
        case SECURITY_ACCESS_RESULT_SEQUENCE_ERROR:
            return UDS_NRC_RequestSequenceError;
        case SECURITY_ACCESS_RESULT_INVALID_ARG:
            return UDS_NRC_IncorrectMessageLengthOrInvalidFormat;
        case SECURITY_ACCESS_RESULT_ENTROPY_UNAVAILABLE:
            return UDS_NRC_ConditionsNotCorrect;
        case SECURITY_ACCESS_RESULT_OK:
        default:
            return UDS_NRC_ConditionsNotCorrect;
    }
}

static UDSErr_t handle_session(UDSServer_t *server,
                               UDSDiagSessCtrlArgs_t *args)
{
    if (args->type != UDS_LEV_DS_DS && args->type != UDS_LEV_DS_PRGS)
    {
        return UDS_NRC_SubFunctionNotSupported;
    }
    if (server->sessionType != args->type)
    {
        SecurityAccess_ClearUnlock();
        if (server->sessionType == UDS_LEV_DS_PRGS)
        {
            Download_Abort();
        }
    }
    args->p2_ms = P2_SERVER_DEFAULT_MS;
    args->p2_star_ms = P2_STAR_SERVER_DEFAULT_MS;
    return UDS_PositiveResponse;
}

static UDSErr_t handle_reset(UDSServer_t *server, UDSECUResetArgs_t *args)
{
    UDSErr_t result = require_programming_and_unlock(server, true);

    if (result != UDS_PositiveResponse)
    {
        return result;
    }
    if (args->type != UDS_LEV_RT_HR)
    {
        return UDS_NRC_SubFunctionNotSupported;
    }
    return UDS_PositiveResponse;
}

static UDSErr_t handle_read_did(UDSServer_t *server, UDSRDBIArgs_t *args)
{
    uint8_t data[UDS_READ_DID_PAYLOAD_MAX_SIZE];
    uds_read_did_payload_t payload = {
        data,
        sizeof(data),
        0U,
    };
    uds_read_did_result_t result;

    result = UDS_ReadDid_BuildPayload(args->dataId, &payload);
    if (result == UDS_READ_DID_RESULT_OUT_OF_RANGE)
    {
        return UDS_NRC_RequestOutOfRange;
    }
    if (result != UDS_READ_DID_RESULT_OK)
    {
        return UDS_NRC_GeneralProgrammingFailure;
    }
    return args->copy(server, payload.data, payload.length);
}

static UDSErr_t handle_seed(UDSServer_t *server,
                            UDSSecAccessRequestSeedArgs_t *args)
{
    uint8_t seed[SECURITY_ACCESS_SEED_SIZE];
    security_access_result_t result;

    if (server->sessionType != UDS_LEV_DS_PRGS)
    {
        return UDS_NRC_SubFunctionNotSupportedInActiveSession;
    }
    if (args->level != SECURITY_ACCESS_LEVEL_PROGRAMMING_SEED ||
        args->len != 0U)
    {
        return UDS_NRC_IncorrectMessageLengthOrInvalidFormat;
    }
    result = SecurityAccess_RequestSeed(seed);
    if (result != SECURITY_ACCESS_RESULT_OK)
    {
        return security_result_to_nrc(result);
    }
    return args->copySeed(server, seed, sizeof(seed));
}

static UDSErr_t handle_key(UDSServer_t *server,
                           UDSSecAccessValidateKeyArgs_t *args)
{
    security_access_result_t result;

    if (server->sessionType != UDS_LEV_DS_PRGS)
    {
        return UDS_NRC_SubFunctionNotSupportedInActiveSession;
    }
    if (args->level != SECURITY_ACCESS_LEVEL_PROGRAMMING_SEED ||
        args->len == 0U)
    {
        return UDS_NRC_IncorrectMessageLengthOrInvalidFormat;
    }
    result = SecurityAccess_SubmitKey(args->key, args->len);
    return result == SECURITY_ACCESS_RESULT_OK
               ? UDS_PositiveResponse
               : security_result_to_nrc(result);
}

static UDSErr_t handle_routine(UDSServer_t *server,
                               UDSRoutineCtrlArgs_t *args)
{
    static const uint8_t success_record[4] = {0U, 0U, 0U, 0U};
    UDSErr_t access = require_programming_and_unlock(server, true);

    if (access != UDS_PositiveResponse)
    {
        return access;
    }
    if (args->id != ROUTINE_ID_ERASE_MEMORY)
    {
        return UDS_NRC_RequestOutOfRange;
    }
    if (args->len != 0U)
    {
        return UDS_NRC_IncorrectMessageLengthOrInvalidFormat;
    }
    if (args->ctrlType == UDS_LEV_RCTP_STR)
    {
        download_result_t result;

        Download_Abort();
        result = Download_Prepare();
        return result == DOWNLOAD_RESULT_OK
                   ? UDS_PositiveResponse
                   : download_result_to_nrc(result);
    }
    if (args->ctrlType != UDS_LEV_RCTP_RRR)
    {
        return UDS_NRC_SubFunctionNotSupported;
    }
    switch (Download_GetPreparationStatus())
    {
        case DOWNLOAD_PREPARATION_READY:
            return args->copyStatusRecord(
                server, success_record, sizeof(success_record));
        case DOWNLOAD_PREPARATION_FAILED:
            return UDS_NRC_GeneralProgrammingFailure;
        case DOWNLOAD_PREPARATION_PENDING:
        case DOWNLOAD_PREPARATION_IDLE:
        default:
            return UDS_NRC_RequestSequenceError;
    }
}

static UDSErr_t handle_download(UDSServer_t *server,
                                UDSRequestDownloadArgs_t *args)
{
    download_result_t result;
    UDSErr_t access = require_programming_and_unlock(server, false);

    if (access != UDS_PositiveResponse)
    {
        return access;
    }
    if (args->dataFormatIdentifier != DOWNLOAD_DATA_FORMAT_ID ||
        (uintptr_t)args->addr != DOWNLOAD_MEMORY_ADDRESS ||
        args->size > UINT32_MAX)
    {
        return UDS_NRC_RequestOutOfRange;
    }
    result = Download_Begin((uint32_t)args->size);
    if (result != DOWNLOAD_RESULT_OK)
    {
        return download_result_to_nrc(result);
    }
    args->maxNumberOfBlockLength = DOWNLOAD_MAX_BLOCK_LENGTH;
    return UDS_PositiveResponse;
}

static UDSErr_t handle_transfer(UDSServer_t *server,
                                UDSTransferDataArgs_t *args)
{
    download_result_t result;
    UDSErr_t access = require_programming_and_unlock(server, false);
    uint8_t block_sequence =
        (uint8_t)(server->xferBlockSequenceCounter - 1U);

    if (access != UDS_PositiveResponse)
    {
        return access;
    }
    result = Download_Transfer(block_sequence, args->data, args->len);
    return result == DOWNLOAD_RESULT_OK
               ? UDS_PositiveResponse
               : download_result_to_nrc(result);
}

static UDSErr_t handle_transfer_exit(
    UDSServer_t *server,
    UDSRequestTransferExitArgs_t *args)
{
    download_result_t result;
    UDSErr_t access = require_programming_and_unlock(server, false);

    if (access != UDS_PositiveResponse)
    {
        return access;
    }
    if (args->len != 0U)
    {
        return UDS_NRC_IncorrectMessageLengthOrInvalidFormat;
    }
    result = Download_Exit();
    return result == DOWNLOAD_RESULT_OK
               ? UDS_PositiveResponse
               : download_result_to_nrc(result);
}

static UDSErr_t server_event(UDSServer_t *server, UDSEvent_t event,
                             void *argument)
{
    switch (event)
    {
        case UDS_EVT_DiagSessCtrl:
            return handle_session(server, argument);
        case UDS_EVT_EcuReset:
            return handle_reset(server, argument);
        case UDS_EVT_ReadDataByIdent:
            return handle_read_did(server, argument);
        case UDS_EVT_SecAccessRequestSeed:
            return handle_seed(server, argument);
        case UDS_EVT_SecAccessValidateKey:
            return handle_key(server, argument);
        case UDS_EVT_RoutineCtrl:
            return handle_routine(server, argument);
        case UDS_EVT_RequestDownload:
            return handle_download(server, argument);
        case UDS_EVT_TransferData:
            return handle_transfer(server, argument);
        case UDS_EVT_RequestTransferExit:
            return handle_transfer_exit(server, argument);
        case UDS_EVT_SessionTimeout:
            SecurityAccess_ClearUnlock();
            Download_Abort();
            return UDS_PositiveResponse;
        case UDS_EVT_DoScheduledReset:
            *(uint8_t *)argument = 0U;
            s_reset_accepted = true;
            return UDS_PositiveResponse;
        case UDS_EVT_Err:
            GW_LOG_E("iso14229 server error=%d", (int)*(UDSErr_t *)argument);
            return UDS_PositiveResponse;
        case UDS_EVT_Custom:
        default:
            return UDS_NRC_ServiceNotSupported;
    }
}

void UDS_Init(UDSTp_t *transport)
{
    s_reset_accepted = false;
    s_now_ms = 0U;
    SecurityAccess_Init();
    Download_Init();
    (void)UDSServerInit(&s_server);
    s_server.tp = transport;
    s_server.fn = server_event;
}

void UDS_Poll(uint32_t now_ms)
{
    s_now_ms = now_ms;
    UDSServerPoll(&s_server);
}

bool UDS_ConsumeAcceptedReset(void)
{
    bool accepted = s_reset_accepted;

    s_reset_accepted = false;
    return accepted;
}
