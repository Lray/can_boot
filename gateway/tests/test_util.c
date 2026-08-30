#include <assert.h>
#include <stdint.h>
#include <string.h>

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

static void test_is_lower_hex(void)
{
    assert(is_lower_hex("0123456789abcdef", 16u));
    assert(is_lower_hex("deadbeef", 8u));
    assert(!is_lower_hex("DEADBEEF", 8u));
    assert(!is_lower_hex("deadbeeg", 8u));
    assert(!is_lower_hex("dead", 8u));
    assert(!is_lower_hex(NULL, 8u));
}

static void test_is_uuid_v4(void)
{
    assert(is_uuid_v4("f81d4fae-7dec-4d0b-a765-00a0c91e6bf6"));
    assert(!is_uuid_v4("f81d4fae-7dec-3d0b-a765-00a0c91e6bf6"));
    assert(!is_uuid_v4("f81d4fae-7dec-4d0b-c765-00a0c91e6bf6"));
    assert(!is_uuid_v4("f81d4fae7dec4d0ba76500a0c91e6bf6"));
    assert(!is_uuid_v4("f81d4fae-7dec-4d0b-a765-00a0c91e6bf6-"));
    assert(!is_uuid_v4(NULL));
}

static void test_valid_ifname(void)
{
    assert(valid_ifname("awlink0"));
    assert(valid_ifname("can0"));
    assert(valid_ifname("lo"));
    assert(valid_ifname("if_1-x.y"));
    assert(!valid_ifname(""));
    assert(!valid_ifname("0123456789abcdef"));
    assert(!valid_ifname("bad/name"));
    assert(!valid_ifname(NULL));
}

int main(void)
{
    test_secure_zero();
    test_is_lower_hex();
    test_is_uuid_v4();
    test_valid_ifname();
    return 0;
}
