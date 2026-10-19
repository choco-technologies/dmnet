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
#include "dmsecrets.h"
#include <errno.h>
#include <string.h>

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

/* The saved networks (a vault of dmsecrets): without a path for the vault the commands
 * fail before they ask for anything */

DMOD_TEST_STEP(saved_networks_need_a_vault)
{
    char* saved[] = { "wifi", "saved" };
    char* forget[] = { "wifi", "forget", "Home" };
    char* save[] = { "wifi", "save", "Home", "secret123" };

    Dmod_SetEnv("DMSECRETS_PATH", "", 1);
    Dmod_SetEnv("DMSECRETS_WIFI_PATH", "", 1);
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("wifi", 2, saved), 0);
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("wifi", 3, forget), 0);
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("wifi", 4, save), 0);
}

DMOD_TEST_STEP(saved_networks_need_an_existing_vault_to_list)
{
    char* saved[] = { "wifi", "saved" };

    Dmod_SetEnv("DMSECRETS_WIFI_PATH", "/tmp/wifi_test_no_such.vault", 1);
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("wifi", 2, saved), 0);
    Dmod_SetEnv("DMSECRETS_WIFI_PATH", "", 1);
}

DMOD_TEST_STEP(forget_and_save_need_a_name)
{
    char* forget[] = { "wifi", "forget" };
    char* save[] = { "wifi", "save" };

    DMOD_TEST_EXPECT_NE(Dmod_RunModule("wifi", 2, forget), 0);
    DMOD_TEST_EXPECT_NE(Dmod_RunModule("wifi", 2, save), 0);
}

DMOD_TEST_STEP(the_saved_networks_vault_is_found_by_the_wifi_domain)
{
    char path[64];

    /* The tool opens its vault with the domain "wifi", as wifid does (this also makes the test
     * import dmsecrets, so that dmf-get installs the tool's own dependency) */
    Dmod_SetEnv("DMSECRETS_WIFI_PATH", "/mnt/networks.vault", 1);
    DMOD_TEST_EXPECT_EQ(dmsecrets_resolve_path(NULL, "wifi", path, sizeof(path)), 0);
    DMOD_TEST_EXPECT_EQ(strcmp(path, "/mnt/networks.vault"), 0);
    Dmod_SetEnv("DMSECRETS_WIFI_PATH", "", 1);
}
