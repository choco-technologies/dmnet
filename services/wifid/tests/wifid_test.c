/**
 * @file wifid_test.c
 * @brief Test steps for wifid
 *
 * Runs the actual `wifid` module through Dmod_RunModule(). The fixture interface is
 * backed by "/dev/null": every ioctl hits dmod's weak default and fails, so it is not
 * a Wi-Fi interface - the case wifid must leave alone at once (it is started for every
 * interface). What it does on a real Wi-Fi interface (scan, pick the saved network in
 * range, connect) needs a radio and is checked on a board.
 */
#include "dmod_test.h"
#include "dmnetif.h"
#include "dmsecrets.h"
#include <string.h>
#include <errno.h>

static dmnetif_iface_t g_iface = NULL;

void dmod_test_setup(void)
{
    g_iface = dmnetif_register("test0", "/dev/null");
}

void dmod_test_teardown(void)
{
    dmnetif_unregister(g_iface);
    g_iface = NULL;
}

DMOD_TEST_STEP(an_interface_that_is_not_wifi_ends_at_once)
{
    char* argv[] = { "wifid", "test0" };

    DMOD_TEST_EXPECT_EQ(Dmod_RunModule("wifid", 2, argv), 0);
    DMOD_TEST_EXPECT_FALSE(Dmod_IsModuleLoaded("wifid"));
}

DMOD_TEST_STEP(an_unknown_interface_is_an_error)
{
    char* argv[] = { "wifid", "does-not-exist" };

    DMOD_TEST_EXPECT_NE(Dmod_RunModule("wifid", 2, argv), 0);
}

DMOD_TEST_STEP(an_interface_name_is_required)
{
    char* argv[] = { "wifid" };

    DMOD_TEST_EXPECT_NE(Dmod_RunModule("wifid", 1, argv), 0);
}

DMOD_TEST_STEP(the_vault_of_the_networks_is_found_by_the_wifi_domain)
{
    char path[64];

    /* wifid opens the vault with the domain "wifi": where it looks (this also makes the test
     * import dmsecrets, which is how dmf-get comes to install wifid's own dependency) */
    Dmod_SetEnv("DMSECRETS_PATH", "/mnt/default.vault", 1);
    Dmod_SetEnv("DMSECRETS_WIFI_PATH", "/mnt/networks.vault", 1);
    DMOD_TEST_EXPECT_EQ(dmsecrets_resolve_path(NULL, "wifi", path, sizeof(path)), 0);
    DMOD_TEST_EXPECT_EQ(strcmp(path, "/mnt/networks.vault"), 0);
    Dmod_SetEnv("DMSECRETS_WIFI_PATH", "", 1);
    DMOD_TEST_EXPECT_EQ(dmsecrets_resolve_path(NULL, "wifi", path, sizeof(path)), 0);
    DMOD_TEST_EXPECT_EQ(strcmp(path, "/mnt/default.vault"), 0);
    Dmod_SetEnv("DMSECRETS_PATH", "", 1);
    DMOD_TEST_EXPECT_EQ(dmsecrets_resolve_path(NULL, "wifi", path, sizeof(path)), -ENOENT);
}
