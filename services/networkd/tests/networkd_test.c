/**
 * @file networkd_test.c
 * @brief Test steps for networkd
 *
 * networkd's own main() is a real C `main` symbol and can't be linked
 * into this test binary (dmod_add_test()'s own harness already provides
 * one) - so these steps exercise the same building blocks main() itself
 * calls (dmnetbridge_reset(), dmnetif_for_each(), dmnetbridge_handle_netif_rx())
 * directly, rather than main(). See src/networkd.c.
 *
 * Same "/null"-backed fixture pattern as lib/dmnetbridge/tests/
 * dmnetbridge_test.c - no real driver means dmnetbridge_handle_netif_rx()
 * spins forever against it, so it's run in a background thread and
 * forcefully stopped rather than joined.
 */
#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmnetif.h"
#include "dmnetbridge.h"
#include "dmosi.h"

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

DMOD_TEST_STEP(reset_is_safe_before_any_pump_started)
{
    dmnetbridge_reset();
    dmnetbridge_reset();
}

typedef struct
{
    size_t count;
} visit_ctx_t;

static bool count_visitor(dmnetif_iface_t iface, void* user_data)
{
    (void)iface;
    ((visit_ctx_t*)user_data)->count++;
    return true;
}

DMOD_TEST_STEP(for_each_visits_the_fixture_interface)
{
    /* main() enumerates exactly this way to decide which interfaces get a
     * pump thread. */
    visit_ctx_t ctx = { .count = 0 };
    dmnetif_for_each(count_visitor, &ctx);
    DMOD_TEST_EXPECT_EQ(ctx.count, (size_t)1);
}

static void pump_thread_entry(void* arg)
{
    dmnetbridge_handle_netif_rx((dmnetif_iface_t)arg);
}

DMOD_TEST_STEP(handle_netif_rx_can_be_started_and_stopped_for_the_fixture)
{
    dmosi_thread_t thread = dmosi_thread_create(pump_thread_entry, g_iface, 0, 4096, "test-pump", NULL);
    DMOD_TEST_EXPECT_NOT_NULL(thread);

    dmosi_thread_sleep(50);
    dmosi_thread_kill(thread, 0);
    dmosi_thread_destroy(thread);

    dmnetbridge_reset();
}
