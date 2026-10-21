#include <assert.h>
#include <stdint.h>
#include "util.h"

static void test_secure_zero(void)
{
    uint8_t data[8] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};

    secure_zero(data, sizeof(data));
    for (size_t index = 0u; index < sizeof(data); ++index)
    {
        assert(data[index] == 0u);
    }
    secure_zero(data, 0u);
    assert(data[0] == 0u);
}

int main(void)
{
    test_secure_zero();
    return 0;
}
