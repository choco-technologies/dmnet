/**
 * @file ip_test.c
 * @brief Test steps for ip
 *
 * Registers a "/dev/null"-backed dmnetif fixture (same pattern as
 * tools/ifconfig/tests/ifconfig_test.c), then drives the actual `ip`
 * module through Dmod_RunModule("ip", argc, argv) so it never stays
 * resident beyond the single call under test. dmnetif/dmroute themselves
 * stay loaded across steps (this test module links them directly), so
 * fixtures set up here are still visible to each Dmod_RunModule() call -
 * including dmroute's automatic connected-route registration, which fires
 * from dmnetif_set_ip_address() below regardless of `ip` ever running.
 */
#include "dmod_test.h"
#include "dmroute.h"
#include "dmnetif.h"
#include "dmip.h"
#include <string.h>

#define TEST_DEVICE_PATH "/dev/null"

static dmnetif_iface_t g_iface = NULL;

static dmip_addr_t make_v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    dmip_addr_t ip = { 0 };
    ip.family = dmip_family_v4;
    ip.addr.v4[0] = a;
    ip.addr.v4[1] = b;
    ip.addr.v4[2] = c;
    ip.addr.v4[3] = d;
    return ip;
}

static bool capture_first_route(dmroute_route_t route, void* user_data)
{
    *(dmroute_route_t*)user_data = route;
    return false;
}

static void clear_all_routes(void)
{
    while (dmroute_count() > 0)
    {
        dmroute_route_t route = NULL;
        dmroute_for_each(capture_first_route, &route);
        if (route == NULL)
            break;
        dmroute_remove(route);
    }
}

void dmod_test_setup(void)
{
    g_iface = dmnetif_register("test0", TEST_DEVICE_PATH);
}

void dmod_test_teardown(void)
{
    clear_all_routes();
    dmnetif_unregister(g_iface);
    g_iface = NULL;
}

/* ---- show / help ---- */

DMOD_TEST_STEP(no_args_shows_usage_and_fails)
{
    char* argv[] = { "ip" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("ip", 1, argv), 0);
}

DMOD_TEST_STEP(help_flag_succeeds)
{
    char* argv[] = { "ip", "--help" };
    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("ip", 2, argv), 0);
}

DMOD_TEST_STEP(run_module_unloads_ip_afterwards)
{
    char* argv[] = { "ip", "--help" };
    Dmod_RunModule("ip", 2, argv);
    DMOD_TEST_EXPECT_FALSE(Dmod_IsModuleLoaded("ip"));
}

DMOD_TEST_STEP(route_show_with_empty_table_succeeds)
{
    char* argv[] = { "ip", "route", "show" };
    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("ip", 3, argv), 0);
}

DMOD_TEST_STEP(route_with_no_verb_defaults_to_show)
{
    char* argv[] = { "ip", "route" };
    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("ip", 2, argv), 0);
}

DMOD_TEST_STEP(unknown_object_fails)
{
    char* argv[] = { "ip", "addr" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("ip", 2, argv), 0);
}

/* ---- route add ---- */

DMOD_TEST_STEP(route_add_with_prefix_succeeds)
{
    char* argv[] = { "ip", "route", "add", "10.0.0.0/8", "dev", "test0" };
    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("ip", 6, argv), 0);

    dmip_addr_t target = make_v4(10, 1, 2, 3);
    DMOD_TEST_EXPECT_NOT_NULL(dmroute_lookup(&target));
}

DMOD_TEST_STEP(route_add_without_prefix_adds_host_route)
{
    char* argv[] = { "ip", "route", "add", "10.5.5.5", "dev", "test0" };
    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("ip", 6, argv), 0);

    dmip_addr_t host = make_v4(10, 5, 5, 5);
    dmroute_route_t route = dmroute_lookup(&host);
    DMOD_TEST_EXPECT_NOT_NULL(route);
    if (route != NULL)
    {
        dmip_addr_t netmask = { 0 };
        DMOD_TEST_EXPECT_EQ(dmroute_get_netmask(route, &netmask), 0);
        DMOD_TEST_EXPECT_EQ(netmask.addr.v4[3], (uint8_t)255);
    }

    dmip_addr_t neighbor = make_v4(10, 5, 5, 6);
    DMOD_TEST_EXPECT_NULL(dmroute_lookup(&neighbor));
}

DMOD_TEST_STEP(route_add_with_gateway_and_metric_succeeds)
{
    char* argv[] = { "ip", "route", "add", "default", "via", "192.168.1.1", "dev", "test0", "metric", "50" };
    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("ip", 10, argv), 0);

    dmip_addr_t target = make_v4(8, 8, 8, 8);
    dmroute_route_t route = dmroute_lookup(&target);
    DMOD_TEST_EXPECT_NOT_NULL(route);
    if (route != NULL)
    {
        dmip_addr_t gateway = { 0 };
        DMOD_TEST_EXPECT_EQ(dmroute_get_gateway(route, &gateway), 0);
        DMOD_TEST_EXPECT_EQ(gateway.family, dmip_family_v4);
        DMOD_TEST_EXPECT_EQ(gateway.addr.v4[3], (uint8_t)1);
        DMOD_TEST_EXPECT_EQ(dmroute_get_metric(route), (uint32_t)50);
    }
}

DMOD_TEST_STEP(route_add_without_dev_fails)
{
    char* argv[] = { "ip", "route", "add", "10.0.0.0/8" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("ip", 4, argv), 0);
}

DMOD_TEST_STEP(route_add_unknown_interface_fails)
{
    char* argv[] = { "ip", "route", "add", "10.0.0.0/8", "dev", "does-not-exist" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("ip", 6, argv), 0);
}

DMOD_TEST_STEP(route_add_invalid_destination_fails)
{
    char* argv[] = { "ip", "route", "add", "not-an-address", "dev", "test0" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("ip", 6, argv), 0);
}

DMOD_TEST_STEP(route_add_prefix_out_of_range_fails)
{
    char* argv[] = { "ip", "route", "add", "10.0.0.0/33", "dev", "test0" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("ip", 6, argv), 0);
}

/* ---- route del ---- */

DMOD_TEST_STEP(route_del_removes_matching_route)
{
    char* add_argv[] = { "ip", "route", "add", "10.0.0.0/8", "dev", "test0" };
    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("ip", 6, add_argv), 0);

    size_t before = dmroute_count();

    char* del_argv[] = { "ip", "route", "del", "10.0.0.0/8", "dev", "test0" };
    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("ip", 6, del_argv), 0);

    DMOD_TEST_EXPECT_EQ(dmroute_count(), before - 1);

    dmip_addr_t target = make_v4(10, 1, 2, 3);
    DMOD_TEST_EXPECT_NULL(dmroute_lookup(&target));
}

DMOD_TEST_STEP(route_del_no_match_fails)
{
    char* argv[] = { "ip", "route", "del", "192.168.99.0/24", "dev", "test0" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("ip", 6, argv), 0);
}

DMOD_TEST_STEP(route_del_without_dev_fails)
{
    char* argv[] = { "ip", "route", "del", "10.0.0.0/8" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("ip", 4, argv), 0);
}

/* ---- route show <dest> / route get ---- */

DMOD_TEST_STEP(route_show_destination_finds_match)
{
    char* add_argv[] = { "ip", "route", "add", "10.0.0.0/8", "dev", "test0" };
    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("ip", 6, add_argv), 0);

    char* show_argv[] = { "ip", "route", "show", "10.1.2.3" };
    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("ip", 4, show_argv), 0);
}

DMOD_TEST_STEP(route_show_destination_no_match_fails)
{
    char* argv[] = { "ip", "route", "show", "192.168.50.1" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("ip", 4, argv), 0);
}

DMOD_TEST_STEP(route_get_is_equivalent_to_show)
{
    char* add_argv[] = { "ip", "route", "add", "10.0.0.0/8", "dev", "test0" };
    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("ip", 6, add_argv), 0);

    char* get_argv[] = { "ip", "route", "get", "10.1.2.3" };
    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("ip", 4, get_argv), 0);
}

/* ---- automatic registration (dmroute's dmnetif DIF, exercised without ip at all) ---- */

DMOD_TEST_STEP(assigning_ip_registers_connected_route_visible_to_ip_route_show)
{
    dmip_addr_t addr = make_v4(192, 168, 1, 42);
    DMOD_TEST_EXPECT_EQ(dmnetif_set_ip_address(g_iface, &addr), 0);

    char* argv[] = { "ip", "route", "show", "192.168.1.42" };
    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("ip", 4, argv), 0);
}
