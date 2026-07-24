/**
 * @file ifconfig_test.c
 * @brief Test steps for ifconfig
 *
 * Registers a "/dev/null"-backed dmnetif fixture (real enough for
 * Dmod_FileOpen() to succeed without needing an actual dmdrvi-backed
 * network driver), then drives the actual `ifconfig` module through
 * Dmod_RunModule("ifconfig", argc, argv) - it loads the module, calls its
 * main(), and unloads it again before returning (see Dmod_RunModule in
 * dmod.h), so `ifconfig` never stays resident beyond the single call under
 * test. dmnetif itself stays loaded across steps (this test module links
 * it directly), so the fixture registered here is still visible to each
 * Dmod_RunModule() invocation.
 *
 * No real driver is loaded, so every DMDRVI_IOCTL_NET_* call dmnetif makes
 * against the fixture hits dmod's weak default Dmod_Ioctl(), which always
 * returns -ENOSYS. That means "up"/"down"/"hw ether" genuinely cannot
 * succeed here - the steps below assert the honest outcome (ifconfig
 * reports failure, state stays consistent), not a fabricated success.
 */
#include "dmod_test.h"
#include "dmnetif.h"
#include <string.h>

#define TEST_DEVICE_PATH "/dev/null"

static dmnetif_iface_t g_iface = NULL;

void dmod_test_setup(void)
{
    g_iface = dmnetif_register("test0", TEST_DEVICE_PATH);

    /* So "show"/"list" steps below exercise the inet-address print path too. */
    dmnetif_ip_addr_t ip = { 0 };
    ip.family = dmnetif_ip_family_v4;
    ip.addr.v4[0] = 192;
    ip.addr.v4[1] = 168;
    ip.addr.v4[2] = 1;
    ip.addr.v4[3] = 42;
    dmnetif_set_ip_address(g_iface, &ip);
}

void dmod_test_teardown(void)
{
    dmnetif_unregister(g_iface);
    g_iface = NULL;
}

DMOD_TEST_STEP(no_args_lists_interfaces)
{
    char* argv[] = { "ifconfig" };
    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("ifconfig", 1, argv), 0);
}

DMOD_TEST_STEP(run_module_unloads_ifconfig_afterwards)
{
    char* argv[] = { "ifconfig" };
    Dmod_RunModule("ifconfig", 1, argv);
    DMOD_TEST_EXPECT_FALSE(Dmod_IsModuleLoaded("ifconfig"));
}

DMOD_TEST_STEP(show_known_interface_succeeds)
{
    char* argv[] = { "ifconfig", "test0" };
    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("ifconfig", 2, argv), 0);
}

DMOD_TEST_STEP(show_unknown_interface_fails)
{
    char* argv[] = { "ifconfig", "does-not-exist" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("ifconfig", 2, argv), 0);
}

DMOD_TEST_STEP(up_reports_driver_failure_without_a_real_driver)
{
    char* argv[] = { "ifconfig", "test0", "up" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("ifconfig", 3, argv), 0);
    DMOD_TEST_EXPECT_FALSE(dmnetif_is_up(g_iface));
}

DMOD_TEST_STEP(down_reports_driver_failure_but_still_marks_interface_down)
{
    char* argv[] = { "ifconfig", "test0", "down" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("ifconfig", 3, argv), 0);
    DMOD_TEST_EXPECT_FALSE(dmnetif_is_up(g_iface));
}

DMOD_TEST_STEP(hw_ether_valid_mac_reports_driver_failure_without_a_real_driver)
{
    /* parses fine, but dmnetif_set_mac_address() -> Dmod_Ioctl() -> -ENOSYS */
    char* argv[] = { "ifconfig", "test0", "hw", "ether", "02:00:00:00:00:09" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("ifconfig", 5, argv), 0);
}

DMOD_TEST_STEP(hw_ether_invalid_mac_syntax_fails)
{
    char* argv[] = { "ifconfig", "test0", "hw", "ether", "not-a-mac" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("ifconfig", 5, argv), 0);
}

DMOD_TEST_STEP(hw_without_ether_keyword_fails)
{
    char* argv[] = { "ifconfig", "test0", "hw", "wrong", "02:00:00:00:00:09" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("ifconfig", 5, argv), 0);
}

DMOD_TEST_STEP(unknown_command_fails)
{
    char* argv[] = { "ifconfig", "test0", "frobnicate" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("ifconfig", 3, argv), 0);
}
