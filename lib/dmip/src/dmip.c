/**
 * @file dmip.c
 * @brief DMOD IP protocol types - lifecycle stubs
 *
 * dmip.h is a pure type-definitions header (dmip_addr_t and friends) -
 * this module has no state and no public API functions, only the
 * dmod_init()/dmod_deinit() lifecycle every DMOD module must define.
 */
#include "dmod.h"
#include "dmip.h"

int dmod_init(const Dmod_Config_t *Config)
{
    (void)Config;
    return 0;
}

int dmod_deinit(void)
{
    return 0;
}
