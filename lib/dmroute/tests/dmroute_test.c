/**
 * @file dmroute_test.c
 * @brief Test steps for dmroute
 *
 * dmroute has no dependency on dmnetif (see dmroute.h's file comment), so
 * these tests use plain string interface names ("test0"/"test1") rather
 * than real dmnetif fixtures - dmroute never resolves or validates them.
 * The "dmnetif_set_ip_address() adds a connected route" integration is
 * covered by lib/dmnetif/tests/dmnetif_test.c instead, since that's the
 * module that now owns the call to dmroute_add()/_remove().
 */
#include "dmod_test.h"
#include "dmroute.h"
#include <string.h>

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

static bool capture_first(dmroute_route_t route, void* user_data)
{
    *(dmroute_route_t*)user_data = route;
    return false;
}

static void clear_all_routes(void)
{
    while (dmroute_count() > 0)
    {
        dmroute_route_t route = NULL;
        dmroute_for_each(capture_first, &route);
        if (route == NULL)
            break; /* safety net against an infinite loop if capture ever fails */
        dmroute_remove(route);
    }
}

void dmod_test_teardown(void)
{
    clear_all_routes();
}

/* ---- Add / remove ---- */

DMOD_TEST_STEP(add_returns_valid_handle)
{
    dmroute_addr_t dest = make_v4(10, 0, 0, 0);
    dmroute_addr_t mask = make_v4(255, 0, 0, 0);
    dmroute_route_t route = dmroute_add(&dest, &mask, NULL, "test0", DMROUTE_DEFAULT_METRIC, dmroute_origin_static);
    DMOD_TEST_EXPECT_NOT_NULL(route);
}

DMOD_TEST_STEP(add_masks_destination_with_netmask)
{
    /* 10.1.2.3/8 should be stored as the network address 10.0.0.0, not
     * the host address it was given. */
    dmroute_addr_t dest = make_v4(10, 1, 2, 3);
    dmroute_addr_t mask = make_v4(255, 0, 0, 0);
    dmroute_route_t route = dmroute_add(&dest, &mask, NULL, "test0", DMROUTE_DEFAULT_METRIC, dmroute_origin_static);
    DMOD_TEST_EXPECT_NOT_NULL(route);

    dmroute_addr_t stored = { 0 };
    DMOD_TEST_EXPECT_EQ(dmroute_get_destination(route, &stored), 0);
    dmroute_addr_t expected = make_v4(10, 0, 0, 0);
    DMOD_TEST_EXPECT_EQ(stored.addr.v4[0], expected.addr.v4[0]);
    DMOD_TEST_EXPECT_EQ(stored.addr.v4[1], expected.addr.v4[1]);
    DMOD_TEST_EXPECT_EQ(stored.addr.v4[2], expected.addr.v4[2]);
    DMOD_TEST_EXPECT_EQ(stored.addr.v4[3], expected.addr.v4[3]);
}

DMOD_TEST_STEP(add_mismatched_netmask_family_fails)
{
    dmroute_addr_t dest = make_v4(10, 0, 0, 0);
    dmroute_addr_t mask = { 0 };
    mask.family = dmroute_family_v6;
    DMOD_TEST_EXPECT_NULL(dmroute_add(&dest, &mask, NULL, "test0", 0, dmroute_origin_static));
}

DMOD_TEST_STEP(add_null_arguments_fail)
{
    dmroute_addr_t dest = make_v4(10, 0, 0, 0);
    dmroute_addr_t mask = make_v4(255, 0, 0, 0);
    DMOD_TEST_EXPECT_NULL(dmroute_add(NULL, &mask, NULL, "test0", 0, dmroute_origin_static));
    DMOD_TEST_EXPECT_NULL(dmroute_add(&dest, NULL, NULL, "test0", 0, dmroute_origin_static));
    DMOD_TEST_EXPECT_NULL(dmroute_add(&dest, &mask, NULL, NULL, 0, dmroute_origin_static));
}

DMOD_TEST_STEP(remove_removes_route)
{
    dmroute_addr_t dest = make_v4(10, 0, 0, 0);
    dmroute_addr_t mask = make_v4(255, 0, 0, 0);
    dmroute_route_t route = dmroute_add(&dest, &mask, NULL, "test0", 0, dmroute_origin_static);
    DMOD_TEST_EXPECT_NOT_NULL(route);

    size_t before = dmroute_count();
    dmroute_remove(route);
    DMOD_TEST_EXPECT_EQ(dmroute_count(), before - 1);
}

DMOD_TEST_STEP(remove_null_is_safe)
{
    dmroute_remove(NULL);
}

/* ---- Lookup ---- */

DMOD_TEST_STEP(lookup_finds_matching_route)
{
    dmroute_addr_t dest = make_v4(10, 0, 0, 0);
    dmroute_addr_t mask = make_v4(255, 0, 0, 0);
    dmroute_route_t route = dmroute_add(&dest, &mask, NULL, "test0", 0, dmroute_origin_static);
    DMOD_TEST_EXPECT_NOT_NULL(route);

    dmroute_addr_t target = make_v4(10, 1, 2, 3);
    DMOD_TEST_EXPECT_EQ(dmroute_lookup(&target), route);
}

DMOD_TEST_STEP(lookup_no_match_returns_null)
{
    dmroute_addr_t dest = make_v4(10, 0, 0, 0);
    dmroute_addr_t mask = make_v4(255, 0, 0, 0);
    DMOD_TEST_EXPECT_NOT_NULL(dmroute_add(&dest, &mask, NULL, "test0", 0, dmroute_origin_static));

    dmroute_addr_t target = make_v4(192, 168, 1, 1);
    DMOD_TEST_EXPECT_NULL(dmroute_lookup(&target));
}

DMOD_TEST_STEP(lookup_prefers_longest_prefix_match)
{
    dmroute_addr_t wide_dest = make_v4(10, 0, 0, 0);
    dmroute_addr_t wide_mask = make_v4(255, 0, 0, 0);
    dmroute_route_t wide = dmroute_add(&wide_dest, &wide_mask, NULL, "test0", 0, dmroute_origin_static);

    dmroute_addr_t narrow_dest = make_v4(10, 1, 2, 0);
    dmroute_addr_t narrow_mask = make_v4(255, 255, 255, 0);
    dmroute_route_t narrow = dmroute_add(&narrow_dest, &narrow_mask, NULL, "test1", 0, dmroute_origin_static);

    DMOD_TEST_EXPECT_NOT_NULL(wide);
    DMOD_TEST_EXPECT_NOT_NULL(narrow);

    dmroute_addr_t target = make_v4(10, 1, 2, 3);
    DMOD_TEST_EXPECT_EQ(dmroute_lookup(&target), narrow);
}

DMOD_TEST_STEP(lookup_breaks_prefix_tie_with_lower_metric)
{
    dmroute_addr_t dest = make_v4(10, 0, 0, 0);
    dmroute_addr_t mask = make_v4(255, 0, 0, 0);

    dmroute_route_t high_metric = dmroute_add(&dest, &mask, NULL, "test0", 100, dmroute_origin_static);
    dmroute_route_t low_metric = dmroute_add(&dest, &mask, NULL, "test1", 10, dmroute_origin_static);
    DMOD_TEST_EXPECT_NOT_NULL(high_metric);
    DMOD_TEST_EXPECT_NOT_NULL(low_metric);

    dmroute_addr_t target = make_v4(10, 1, 2, 3);
    DMOD_TEST_EXPECT_EQ(dmroute_lookup(&target), low_metric);
}

DMOD_TEST_STEP(lookup_invalid_destination_returns_null)
{
    dmroute_addr_t bad = { 0 };
    bad.family = dmroute_family_none;
    DMOD_TEST_EXPECT_NULL(dmroute_lookup(&bad));
    DMOD_TEST_EXPECT_NULL(dmroute_lookup(NULL));
}

/* ---- Accessors ---- */

DMOD_TEST_STEP(accessors_report_added_values)
{
    dmroute_addr_t dest = make_v4(172, 16, 0, 0);
    dmroute_addr_t mask = make_v4(255, 240, 0, 0);
    dmroute_addr_t gw = make_v4(172, 16, 0, 1);
    dmroute_route_t route = dmroute_add(&dest, &mask, &gw, "test0", 42, dmroute_origin_static);
    DMOD_TEST_EXPECT_NOT_NULL(route);

    dmroute_addr_t got_gw = { 0 };
    DMOD_TEST_EXPECT_EQ(dmroute_get_gateway(route, &got_gw), 0);
    DMOD_TEST_EXPECT_EQ(got_gw.family, dmroute_family_v4);
    DMOD_TEST_EXPECT_EQ(got_gw.addr.v4[3], 1);

    const char* iface_name = dmroute_get_iface_name(route);
    DMOD_TEST_EXPECT_NOT_NULL(iface_name);
    if (iface_name != NULL)
    {
        DMOD_TEST_EXPECT_EQ(strcmp(iface_name, "test0"), 0);
    }

    DMOD_TEST_EXPECT_EQ(dmroute_get_metric(route), (uint32_t)42);
    DMOD_TEST_EXPECT_EQ(dmroute_get_origin(route), dmroute_origin_static);
}

DMOD_TEST_STEP(no_gateway_reports_family_none)
{
    dmroute_addr_t dest = make_v4(10, 0, 0, 0);
    dmroute_addr_t mask = make_v4(255, 0, 0, 0);
    dmroute_route_t route = dmroute_add(&dest, &mask, NULL, "test0", 0, dmroute_origin_static);
    DMOD_TEST_EXPECT_NOT_NULL(route);

    dmroute_addr_t got_gw = { 0 };
    DMOD_TEST_EXPECT_EQ(dmroute_get_gateway(route, &got_gw), 0);
    DMOD_TEST_EXPECT_EQ(got_gw.family, dmroute_family_none);
}

DMOD_TEST_STEP(connected_origin_is_reported_back)
{
    dmroute_addr_t dest = make_v4(192, 168, 1, 0);
    dmroute_addr_t mask = make_v4(255, 255, 255, 0);
    dmroute_route_t route = dmroute_add(&dest, &mask, NULL, "test0", DMROUTE_DEFAULT_METRIC, dmroute_origin_connected);
    DMOD_TEST_EXPECT_NOT_NULL(route);
    DMOD_TEST_EXPECT_EQ(dmroute_get_origin(route), dmroute_origin_connected);
}

DMOD_TEST_STEP(does_not_validate_iface_name)
{
    /* dmroute has no dmnetif dependency, so it cannot and does not check
     * that "does-not-exist" names a real interface - that's the caller's
     * job (dmnetif itself for connected routes, `ip route add` for
     * static ones). */
    dmroute_addr_t dest = make_v4(10, 0, 0, 0);
    dmroute_addr_t mask = make_v4(255, 0, 0, 0);
    DMOD_TEST_EXPECT_NOT_NULL(dmroute_add(&dest, &mask, NULL, "does-not-exist", 0, dmroute_origin_static));
}
