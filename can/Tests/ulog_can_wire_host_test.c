#include <assert.h>
#include <string.h>

#include "shared/can_network.h"
#include "ulog_can_wire.h"

static void test_build_frame_is_stateful_fragment_boundary(void)
{
    can_frame_t frame = {0};

    assert(ULogCanWire_BuildFrame("abcdefghijklmn",
                                  14U,
                                  1U,
                                  &frame));
    assert(frame.id == CAN_ID_ECU_ULOG);
    assert(frame.dlc == 8U);
    assert(frame.data[0] == (ECU_LOG_CAN_END | 1U));
    assert(memcmp(&frame.data[1], "hijklmn", 7U) == 0);

    assert(ULogCanWire_BuildFrame("", 0U, 0U, &frame));
    assert(frame.dlc == 1U);
    assert(frame.data[0] == (ECU_LOG_CAN_START | ECU_LOG_CAN_END));
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
