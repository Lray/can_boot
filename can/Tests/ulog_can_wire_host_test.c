#include <assert.h>
#include <string.h>

#include "shared/can_network.h"
#include "ulog_can_wire.h"

static void test_build_frame_is_stateful_fragment_boundary(void)
{
    CO_CANtx_t frame = {
        .ident = CAN_ID_MCU_ULOG(CAN_NODE_ID_MIN),
        .bufferFull = false,
        .syncFlag = true,
    };

    assert(ULogCanWire_BuildFrame("abcdefghijklmn",
                                  14U,
                                  1U,
                                  &frame));
    assert(frame.ident == CAN_ID_MCU_ULOG(CAN_NODE_ID_MIN));
    assert(!frame.bufferFull);
    assert(frame.syncFlag);
    assert(frame.DLC == 8U);
    assert(frame.data[0] == (MCU_LOG_CAN_END | 1U));
    assert(memcmp(&frame.data[1], "hijklmn", 7U) == 0);

    assert(ULogCanWire_BuildFrame("", 0U, 0U, &frame));
    assert(frame.DLC == 1U);
    assert(frame.ident == CAN_ID_MCU_ULOG(CAN_NODE_ID_MIN));
    assert(!frame.bufferFull);
    assert(frame.syncFlag);
    assert(frame.data[0] == (MCU_LOG_CAN_START | MCU_LOG_CAN_END));
    assert(!ULogCanWire_BuildFrame("", 0U, 1U, &frame));
    assert(!ULogCanWire_BuildFrame("abcdefghijklmn",
                                   14U,
                                   2U,
                                   &frame));
}

int main(void)
{
    test_build_frame_is_stateful_fragment_boundary();
    return 0;
}
