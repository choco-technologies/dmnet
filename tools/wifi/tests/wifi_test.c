/**
 * @file wifi_test.c
 * @brief Test steps for wifi
 *
 * Drives the actual `wifi` module through Dmod_RunModule("wifi", argc, argv)
 * (loads it, calls main(), unloads it again). No Wi-Fi driver is loaded, so
 * "/dev/null" opens but every DMDRVI_IOCTL_WIFI_* call hits dmod's weak
 * default Dmod_Ioctl(), which returns -ENOSYS: the steps assert the honest
 * outcome (the tool reports the failure), not a fabricated success.
 */
#include "dmod_test.h"

DMOD_TEST_STEP(help_succeeds)
{
    char* argv[] = { "wifi", "--help" };
    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("wifi", 2, argv), 0);
}

DMOD_TEST_STEP(run_module_unloads_wifi_afterwards)
{
    char* argv[] = { "wifi", "help" };
    Dmod_RunModule("wifi", 2, argv);
    DMOD_TEST_EXPECT_FALSE(Dmod_IsModuleLoaded("wifi"));
}

DMOD_TEST_STEP(missing_device_fails)
{
    char* argv[] = { "wifi", "-d", "/dev/does-not-exist", "status" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("wifi", 4, argv), 0);
}

DMOD_TEST_STEP(status_without_a_driver_fails)
{
    char* argv[] = { "wifi", "-d", "/dev/null", "status" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("wifi", 4, argv), 0);
}

DMOD_TEST_STEP(info_without_a_driver_fails)
{
    char* argv[] = { "wifi", "-d", "/dev/null", "info" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("wifi", 4, argv), 0);
}

DMOD_TEST_STEP(scan_without_a_driver_fails)
{
    char* argv[] = { "wifi", "-d", "/dev/null", "scan" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("wifi", 4, argv), 0);
}

DMOD_TEST_STEP(connect_needs_a_network_name)
{
    char* argv[] = { "wifi", "-d", "/dev/null", "connect" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("wifi", 4, argv), 0);
}

DMOD_TEST_STEP(unknown_command_fails)
{
    char* argv[] = { "wifi", "-d", "/dev/null", "frobnicate" };
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("wifi", 4, argv), 0);
}
