#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmnet.h"

static dmnet_t g_handle = NULL;

void dmod_test_setup(void)
{
    g_handle = dmnet_create();
}

void dmod_test_teardown(void)
{
    dmnet_destroy(g_handle);
    g_handle = NULL;
}

DMOD_TEST_STEP(dmnet_create)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_handle);
}

DMOD_TEST_STEP(dmnet_is_valid)
{
    DMOD_TEST_EXPECT_TRUE(dmnet_is_valid(g_handle));
}

DMOD_TEST_STEP(dmnet_destroy_null)
{
    /* Destroying NULL must not crash. */
    dmnet_destroy(NULL);
}
