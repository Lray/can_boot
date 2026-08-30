#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "can_transport.h"
#include "isotp.h"
#include "isotp_user.h"
#include "stm32u5xx_hal.h"

static CAN_ReturnError_t s_can_result;
static uint32_t s_now_ms;
static uint32_t s_sent_count;
static IsoTpLink s_link;
static uint8_t s_send_buffer[32U];
static uint8_t s_receive_buffer[32U];

static CAN_ReturnError_t TestCanSend(const can_frame_t *frame)
{
    assert(frame != NULL);
    if (s_can_result == CAN_ERROR_NO)
    {
        s_sent_count++;
    }
    return s_can_result;
}

uint32_t HAL_GetTick(void)
{
    return s_now_ms;
}

static void ResetTest(uint32_t now_ms)
{
    memset(&s_link, 0, sizeof(s_link));
    memset(s_send_buffer, 0, sizeof(s_send_buffer));
    memset(s_receive_buffer, 0, sizeof(s_receive_buffer));
    s_now_ms = now_ms;
    s_can_result = CAN_ERROR_NO;
    s_sent_count = 0U;
    CAN_Transport_Init(TestCanSend);
    isotp_init_link(&s_link,
                    0x7E8U,
                    s_send_buffer,
                    sizeof(s_send_buffer),
                    s_receive_buffer,
                    sizeof(s_receive_buffer));
}

static void TestClockUsesMicrosecondUnitsWithMillisecondResolution(void)
{
    ResetTest(123U);
    assert(isotp_user_get_us() == 123000U);
    assert(isotp_user_get_us() == 123000U);
    s_now_ms++;
    assert(isotp_user_get_us() == 124000U);
}

static void TestCanBackpressureUsesUpstreamNoSpaceContract(void)
{
    uint8_t payload[8U] = {0};

    ResetTest(0U);
    s_can_result = CAN_ERROR_TX_BUSY;
    assert(isotp_send(&s_link, payload, sizeof(payload)) ==
           ISOTP_RET_NOSPACE);
    assert(s_sent_count == 0U);
    assert(s_link.send_status != ISOTP_SEND_STATUS_INPROGRESS);
}

int main(void)
{
    TestClockUsesMicrosecondUnitsWithMillisecondResolution();
    TestCanBackpressureUsesUpstreamNoSpaceContract();
    return 0;
}
