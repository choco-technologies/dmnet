/**
 * @file dmnetif_test.c
 * @brief Test steps for dmnetif
 *
 * Exercises the registry (register/unregister/find/count/for_each) against
 * "/dev/null" as a stand-in device file - real enough for Dmod_FileOpen()
 * to succeed without needing an actual dmdrvi-backed network driver.
 * DMDRVI_IOCTL_NET_* driven behavior (up/down/MAC/link status/send/receive)
 * only gets exercised here for the NULL-handle error paths; real coverage
 * of that needs a real network driver behind dmnetif_register().
 */
#include "dmod_test.h"
#include "dmnetif.h"
#include <string.h>

#define TEST_DEVICE_PATH "/null"

static dmnetif_iface_t g_iface = NULL;

void dmod_test_setup(void)
{
    g_iface = dmnetif_register("test0", TEST_DEVICE_PATH);
}

void dmod_test_teardown(void)
{
    dmnetif_unregister(g_iface);
    g_iface = NULL;
}

/* ---- Registration ---- */

DMOD_TEST_STEP(register_returns_valid_handle)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_iface);
}

DMOD_TEST_STEP(register_null_name_fails)
{
    DMOD_TEST_EXPECT_NULL(dmnetif_register(NULL, TEST_DEVICE_PATH));
}

DMOD_TEST_STEP(register_null_device_path_fails)
{
    DMOD_TEST_EXPECT_NULL(dmnetif_register("bad0", NULL));
}

DMOD_TEST_STEP(register_nonexistent_device_fails)
{
    /* fopen(..., "r+") fails for a path that doesn't already exist */
    DMOD_TEST_EXPECT_NULL(dmnetif_register("bad1", "/nonexistent/dmnetif-test-path"));
}

DMOD_TEST_STEP(register_duplicate_name_fails)
{
    DMOD_TEST_EXPECT_NULL(dmnetif_register("test0", TEST_DEVICE_PATH));
    /* the earlier registration must still be the one on record */
    DMOD_TEST_EXPECT_EQ(dmnetif_find_by_name("test0"), g_iface);
}

DMOD_TEST_STEP(unregister_removes_interface)
{
    dmnetif_unregister(g_iface);
    DMOD_TEST_EXPECT_NULL(dmnetif_find_by_name("test0"));
    DMOD_TEST_EXPECT_EQ(dmnetif_count(), (size_t)0);
    g_iface = NULL; /* already unregistered - teardown's unregister(NULL) is a no-op */
}

DMOD_TEST_STEP(unregister_null_is_safe)
{
    dmnetif_unregister(NULL);
}

/* ---- Lookup / enumeration ---- */

DMOD_TEST_STEP(find_by_name_finds_registered_interface)
{
    DMOD_TEST_EXPECT_EQ(dmnetif_find_by_name("test0"), g_iface);
}

DMOD_TEST_STEP(find_by_name_unknown_returns_null)
{
    DMOD_TEST_EXPECT_NULL(dmnetif_find_by_name("does-not-exist"));
}

DMOD_TEST_STEP(count_reflects_registered_interface)
{
    DMOD_TEST_EXPECT_EQ(dmnetif_count(), (size_t)1);
}

DMOD_TEST_STEP(get_name_returns_registered_name)
{
    const char* name = dmnetif_get_name(g_iface);
    DMOD_TEST_EXPECT_NOT_NULL(name);
    if (name != NULL)
    {
        DMOD_TEST_EXPECT_EQ(strcmp(name, "test0"), 0);
    }
}

DMOD_TEST_STEP(get_name_invalid_handle_returns_null)
{
    DMOD_TEST_EXPECT_NULL(dmnetif_get_name(NULL));
}

typedef struct
{
    int             visit_count;
    dmnetif_iface_t last_seen;
} for_each_capture_t;

static bool capture_iface(dmnetif_iface_t iface, void* user_data)
{
    for_each_capture_t* capture = (for_each_capture_t*)user_data;
    capture->visit_count++;
    capture->last_seen = iface;
    return true;
}

DMOD_TEST_STEP(for_each_visits_registered_interface_exactly_once)
{
    for_each_capture_t capture = { 0 };
    dmnetif_for_each(capture_iface, &capture);
    DMOD_TEST_EXPECT_EQ(capture.visit_count, 1);
    DMOD_TEST_EXPECT_EQ(capture.last_seen, g_iface);
}

static bool stop_after_first(dmnetif_iface_t iface, void* user_data)
{
    (void)iface;
    int* visit_count = (int*)user_data;
    (*visit_count)++;
    return false;
}

DMOD_TEST_STEP(for_each_stops_when_callback_returns_false)
{
    int visit_count = 0;
    dmnetif_for_each(stop_after_first, &visit_count);
    DMOD_TEST_EXPECT_EQ(visit_count, 1);
}

/* ---- State control (fresh, never-started interface) ---- */

DMOD_TEST_STEP(is_up_starts_false)
{
    DMOD_TEST_EXPECT_FALSE(dmnetif_is_up(g_iface));
}

/* ---- Invalid-handle error paths ---- */

DMOD_TEST_STEP(invalid_handle_state_control_returns_errors)
{
    DMOD_TEST_EXPECT_TRUE(dmnetif_up(NULL) < 0);
    DMOD_TEST_EXPECT_TRUE(dmnetif_down(NULL) < 0);
    DMOD_TEST_EXPECT_FALSE(dmnetif_is_up(NULL));
    DMOD_TEST_EXPECT_EQ(dmnetif_get_link_status(NULL), dmnetif_link_down);
}

DMOD_TEST_STEP(invalid_handle_mac_address_returns_error)
{
    dmnetif_mac_addr_t mac = { 0 };
    DMOD_TEST_EXPECT_TRUE(dmnetif_get_mac_address(NULL, &mac) < 0);
    DMOD_TEST_EXPECT_TRUE(dmnetif_set_mac_address(NULL, &mac) < 0);
}

DMOD_TEST_STEP(invalid_handle_send_receive_return_zero)
{
    uint8_t buffer[8];
    DMOD_TEST_EXPECT_EQ(dmnetif_send(NULL, buffer, sizeof(buffer)), (size_t)0);
    DMOD_TEST_EXPECT_EQ(dmnetif_receive(NULL, buffer, sizeof(buffer)), (size_t)0);
}

DMOD_TEST_STEP(not_started_interface_send_receive_return_zero)
{
    uint8_t buffer[8] = { 0 };
    DMOD_TEST_EXPECT_EQ(dmnetif_send(g_iface, buffer, sizeof(buffer)), (size_t)0);
    DMOD_TEST_EXPECT_EQ(dmnetif_receive(g_iface, buffer, sizeof(buffer)), (size_t)0);
}

DMOD_TEST_STEP(invalid_handle_ioctl_returns_error)
{
    DMOD_TEST_EXPECT_TRUE(dmnetif_ioctl(NULL, 0, NULL) < 0);
}
