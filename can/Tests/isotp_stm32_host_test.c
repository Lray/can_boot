#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "301/CO_driver.h"
#include "isotp.h"
#include "isotp_stm32.h"
#include "isotp_user.h"
#include "stm32u5xx_hal.h"

static CO_ReturnError_t s_can_result;
static uint32_t s_now_ms;
static uint32_t s_sent_count;
static IsoTpLink s_link;
static uint8_t s_send_buffer[32U];
static uint8_t s_receive_buffer[32U];
static CO_CANmodule_t s_can_module;
static CO_CANtx_t s_tx_buffer;

CO_ReturnError_t CO_CANsend(CO_CANmodule_t *CANmodule, CO_CANtx_t *buffer)
{
    assert(CANmodule == &s_can_module);
    assert(buffer == &s_tx_buffer);
    if (s_can_result == CO_ERROR_NO)
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
    memset(&s_can_module, 0, sizeof(s_can_module));
    memset(&s_tx_buffer, 0, sizeof(s_tx_buffer));
    s_now_ms = now_ms;
    s_can_result = CO_ERROR_NO;
    s_sent_count = 0U;
    s_tx_buffer.ident = 0x7E8U;
    isotp_stm32_init(&s_can_module, &s_tx_buffer);
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
    s_can_result = CO_ERROR_TX_BUSY;
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
