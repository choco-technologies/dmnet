/**
 * @file dmarp_test.c
 * @brief Test steps for dmarp
 *
 * Two fixture interfaces ("test0"/"test1") are registered against
 * "/dev/null" (real enough for Dmod_FileOpen() to succeed without a real
 * driver - same pattern as lib/dmnetif/tests/dmnetif_test.c). No real
 * driver means neither can ever go "up" or answer DMDRVI_IOCTL_NET_*
 * calls (they all hit dmod's weak default Dmod_Ioctl(), which returns
 * -ENOSYS) - dmarp_resolve() on a cache miss against either one always
 * fails fast with -ENODEV (dmnetif_get_mac_address() fails before a
 * request could even be sent), so the reply-wait/timeout path itself is
 * not exercised here. What *is* covered: the cache (insert/lookup/
 * remove/count) directly, and dmarp_resolve()'s cache-hit path, which is
 * exactly the same cache underneath.
 *
 * Every step uses a distinct IP address (never reused across steps) since
 * the cache is keyed by interface *name* - a value that survives a
 * fixture being unregistered and re-registered by consecutive
 * setup/teardown calls - so cache entries genuinely persist across test
 * steps for the same name unless each step's addresses are kept unique.
 */
#include "dmod_test.h"
#include "dmarp.h"
#include "dmnetif.h"
#include <string.h>
#include <errno.h>

#define TEST_DEVICE_PATH_0 "/null"
#define TEST_DEVICE_PATH_1 "/null2"

static dmnetif_iface_t g_iface0 = NULL;
static dmnetif_iface_t g_iface1 = NULL;

static dmroute_addr_t make_v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    dmroute_addr_t ip = { 0 };
    ip.family = dmroute_family_v4;
    ip.addr.v4[0] = a;
    ip.addr.v4[1] = b;
    ip.addr.v4[2] = c;
    ip.addr.v4[3] = d;
    return ip;
}

static dmnetif_mac_addr_t make_mac(uint8_t last_octet)
{
    dmnetif_mac_addr_t mac = { { 0x02, 0x00, 0x00, 0x00, 0x00, last_octet } };
    return mac;
}

/* dmod modules have no libc memcmp() (see dmod/src/module/string.c's
 * minimal replacement set) - a small manual comparison stands in for it. */
static bool mac_equal(const dmnetif_mac_addr_t* a, const dmnetif_mac_addr_t* b)
{
    for (int i = 0; i < DMNETIF_MAC_ADDR_LEN; i++)
    {
        if (a->addr[i] != b->addr[i])
            return false;
    }
    return true;
}

void dmod_test_setup(void)
{
    g_iface0 = dmnetif_register("test0", TEST_DEVICE_PATH_0);
    g_iface1 = dmnetif_register("test1", TEST_DEVICE_PATH_1);
}

void dmod_test_teardown(void)
{
    dmnetif_unregister(g_iface0);
    dmnetif_unregister(g_iface1);
    g_iface0 = NULL;
    g_iface1 = NULL;
}

/* ---- Cache: lookup / insert ---- */

DMOD_TEST_STEP(lookup_unknown_entry_returns_false)
{
    dmroute_addr_t ip = make_v4(10, 0, 0, 1);
    dmnetif_mac_addr_t mac = { 0 };
    DMOD_TEST_EXPECT_FALSE(dmarp_cache_lookup(g_iface0, &ip, &mac));
}

DMOD_TEST_STEP(insert_then_lookup_roundtrips)
{
    dmroute_addr_t ip = make_v4(10, 0, 0, 2);
    dmnetif_mac_addr_t mac = make_mac(0x02);
    dmarp_cache_insert(g_iface0, &ip, &mac);

    dmnetif_mac_addr_t got = { 0 };
    DMOD_TEST_EXPECT_TRUE(dmarp_cache_lookup(g_iface0, &ip, &got));
    DMOD_TEST_EXPECT_TRUE(mac_equal(&got, &mac));

    dmarp_cache_remove(g_iface0, &ip);
}

DMOD_TEST_STEP(insert_replaces_existing_entry_without_growing_cache)
{
    dmroute_addr_t ip = make_v4(10, 0, 0, 3);
    dmnetif_mac_addr_t first_mac = make_mac(0x03);
    dmnetif_mac_addr_t second_mac = make_mac(0x04);

    dmarp_cache_insert(g_iface0, &ip, &first_mac);
    size_t count_after_first = dmarp_cache_count();

    dmarp_cache_insert(g_iface0, &ip, &second_mac);
    DMOD_TEST_EXPECT_EQ(dmarp_cache_count(), count_after_first);

    dmnetif_mac_addr_t got = { 0 };
    DMOD_TEST_EXPECT_TRUE(dmarp_cache_lookup(g_iface0, &ip, &got));
    DMOD_TEST_EXPECT_TRUE(mac_equal(&got, &second_mac));

    dmarp_cache_remove(g_iface0, &ip);
}

DMOD_TEST_STEP(same_ip_on_different_interfaces_are_independent_entries)
{
    dmroute_addr_t ip = make_v4(10, 0, 0, 4);
    dmnetif_mac_addr_t mac0 = make_mac(0x05);
    dmnetif_mac_addr_t mac1 = make_mac(0x06);

    dmarp_cache_insert(g_iface0, &ip, &mac0);
    dmarp_cache_insert(g_iface1, &ip, &mac1);

    dmnetif_mac_addr_t got0 = { 0 };
    dmnetif_mac_addr_t got1 = { 0 };
    DMOD_TEST_EXPECT_TRUE(dmarp_cache_lookup(g_iface0, &ip, &got0));
    DMOD_TEST_EXPECT_TRUE(dmarp_cache_lookup(g_iface1, &ip, &got1));
    DMOD_TEST_EXPECT_TRUE(mac_equal(&got0, &mac0));
    DMOD_TEST_EXPECT_TRUE(mac_equal(&got1, &mac1));

    dmarp_cache_remove(g_iface0, &ip);
    dmarp_cache_remove(g_iface1, &ip);
}

DMOD_TEST_STEP(insert_invalid_arguments_is_noop)
{
    dmroute_addr_t ip = make_v4(10, 0, 0, 5);
    dmnetif_mac_addr_t mac = make_mac(0x07);
    size_t before = dmarp_cache_count();

    dmarp_cache_insert(NULL, &ip, &mac);
    dmarp_cache_insert(g_iface0, NULL, &mac);
    dmarp_cache_insert(g_iface0, &ip, NULL);

    dmroute_addr_t bad_family_ip = ip;
    bad_family_ip.family = dmroute_family_v6;
    dmarp_cache_insert(g_iface0, &bad_family_ip, &mac);

    DMOD_TEST_EXPECT_EQ(dmarp_cache_count(), before);
}

DMOD_TEST_STEP(lookup_invalid_arguments_return_false)
{
    dmroute_addr_t ip = make_v4(10, 0, 0, 6);
    dmnetif_mac_addr_t mac = { 0 };

    DMOD_TEST_EXPECT_FALSE(dmarp_cache_lookup(NULL, &ip, &mac));
    DMOD_TEST_EXPECT_FALSE(dmarp_cache_lookup(g_iface0, NULL, &mac));
    DMOD_TEST_EXPECT_FALSE(dmarp_cache_lookup(g_iface0, &ip, NULL));

    dmroute_addr_t bad_family_ip = ip;
    bad_family_ip.family = dmroute_family_none;
    DMOD_TEST_EXPECT_FALSE(dmarp_cache_lookup(g_iface0, &bad_family_ip, &mac));
}

/* ---- Cache: remove / count ---- */

DMOD_TEST_STEP(remove_removes_entry)
{
    dmroute_addr_t ip = make_v4(10, 0, 0, 7);
    dmnetif_mac_addr_t mac = make_mac(0x08);
    dmarp_cache_insert(g_iface0, &ip, &mac);

    size_t before = dmarp_cache_count();
    dmarp_cache_remove(g_iface0, &ip);
    DMOD_TEST_EXPECT_EQ(dmarp_cache_count(), before - 1);

    dmnetif_mac_addr_t got = { 0 };
    DMOD_TEST_EXPECT_FALSE(dmarp_cache_lookup(g_iface0, &ip, &got));
}

DMOD_TEST_STEP(remove_unknown_entry_is_safe)
{
    dmroute_addr_t ip = make_v4(10, 0, 0, 8);
    size_t before = dmarp_cache_count();

    dmarp_cache_remove(g_iface0, &ip);
    dmarp_cache_remove(NULL, &ip);

    DMOD_TEST_EXPECT_EQ(dmarp_cache_count(), before);
}

/* ---- Resolve ---- */

DMOD_TEST_STEP(resolve_returns_cached_entry_without_sending)
{
    dmroute_addr_t ip = make_v4(10, 0, 0, 9);
    dmnetif_mac_addr_t cached_mac = make_mac(0x09);
    dmarp_cache_insert(g_iface0, &ip, &cached_mac);

    /* g_iface0 can never actually send (no real driver, never brought
     * up) - a cache hit is the only way this can return 0. */
    dmnetif_mac_addr_t resolved = { 0 };
    DMOD_TEST_EXPECT_EQ(dmarp_resolve(g_iface0, &ip, &resolved, DMARP_DEFAULT_TIMEOUT_MS), 0);
    DMOD_TEST_EXPECT_TRUE(mac_equal(&resolved, &cached_mac));

    dmarp_cache_remove(g_iface0, &ip);
}

DMOD_TEST_STEP(resolve_cache_miss_without_real_driver_returns_enodev)
{
    /* No real driver behind the fixture means dmnetif_get_mac_address()
     * itself fails (DMDRVI_IOCTL_NET_GET_MAC_ADDR hits dmod's weak
     * default Dmod_Ioctl(), -ENOSYS) before a request could even be
     * built, so this fails fast rather than waiting out the timeout. */
    dmroute_addr_t ip = make_v4(10, 0, 0, 10);
    dmnetif_mac_addr_t resolved = { 0 };
    DMOD_TEST_EXPECT_EQ(dmarp_resolve(g_iface0, &ip, &resolved, DMARP_DEFAULT_TIMEOUT_MS), -ENODEV);
}

DMOD_TEST_STEP(resolve_null_arguments_return_einval)
{
    dmroute_addr_t ip = make_v4(10, 0, 0, 11);
    dmnetif_mac_addr_t mac = { 0 };

    DMOD_TEST_EXPECT_EQ(dmarp_resolve(NULL, &ip, &mac, DMARP_DEFAULT_TIMEOUT_MS), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmarp_resolve(g_iface0, NULL, &mac, DMARP_DEFAULT_TIMEOUT_MS), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmarp_resolve(g_iface0, &ip, NULL, DMARP_DEFAULT_TIMEOUT_MS), -EINVAL);
}

DMOD_TEST_STEP(resolve_non_v4_family_returns_einval)
{
    dmroute_addr_t ip = make_v4(10, 0, 0, 12);
    ip.family = dmroute_family_v6;
    dmnetif_mac_addr_t mac = { 0 };
    DMOD_TEST_EXPECT_EQ(dmarp_resolve(g_iface0, &ip, &mac, DMARP_DEFAULT_TIMEOUT_MS), -EINVAL);
}
