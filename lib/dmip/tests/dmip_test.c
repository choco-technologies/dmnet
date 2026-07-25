/**
 * @file dmip_test.c
 * @brief Test steps for dmip
 *
 * dmip has no functions, only type definitions - these steps just check
 * the shapes other modules rely on (family enum values, union sizing)
 * stay what dmnetif/dmroute's field-by-field conversions assume.
 */
#include "dmod_test.h"
#include "dmip.h"

DMOD_TEST_STEP(family_enum_values_are_distinct)
{
    DMOD_TEST_EXPECT_NE(dmip_family_none, dmip_family_v4);
    DMOD_TEST_EXPECT_NE(dmip_family_none, dmip_family_v6);
    DMOD_TEST_EXPECT_NE(dmip_family_v4, dmip_family_v6);
}

DMOD_TEST_STEP(addr_union_fits_both_families)
{
    dmip_addr_t addr = { 0 };
    DMOD_TEST_EXPECT_EQ(sizeof(addr.addr.v4), (size_t)DMIP_IPV4_ADDR_LEN);
    DMOD_TEST_EXPECT_EQ(sizeof(addr.addr.v6), (size_t)DMIP_IPV6_ADDR_LEN);
}

DMOD_TEST_STEP(v4_and_v6_share_leading_storage)
{
    /* dmnetif.c/dmroute.c's mask/compare helpers read/write through
     * addr.v6[i] for i < DMIP_IPV4_ADDR_LEN regardless of which member
     * was last assigned - only valid because v4 and v6 are the leading
     * bytes of the same union storage. */
    dmip_addr_t addr = { 0 };
    addr.addr.v4[0] = 0xAB;
    DMOD_TEST_EXPECT_EQ(addr.addr.v6[0], (uint8_t)0xAB);
}
