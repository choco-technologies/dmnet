/**
 * @file dmnetif.c
 * @brief DMOD Network Interface Manager - Implementation
 *
 * Stub implementation - every dmod_dmnetif_api_declaration() below has a
 * placeholder body that compiles and returns a safe default (NULL/false/0/
 * -ENOSYS) rather than real registry/device logic. This establishes the
 * module and its API shape; the real registry (backed by dmlist, per
 * dmnetif.h/docs/dmnetif.md) and the dmdrvi-facing device I/O land in a
 * follow-up.
 */
#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmnetif.h"
#include <errno.h>

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    DMOD_LOG_INFO("DMNETIF interface manager initialized\n");
    return 0;
}

int dmod_deinit(void)
{
    DMOD_LOG_INFO("DMNETIF interface manager deinitialized\n");
    return 0;
}

/* ---- Registration (driver-facing) ---- */

dmod_dmnetif_api_declaration(1.0, dmnetif_iface_t, _register, ( const char* name, const char* device_path ))
{
    DMOD_LOG_ERROR("dmnetif_register not yet implemented (name=%s, device_path=%s)\n",
                    name != NULL ? name : "(null)", device_path != NULL ? device_path : "(null)");
    return NULL;
}

dmod_dmnetif_api_declaration(1.0, void, _unregister, ( dmnetif_iface_t iface ))
{
    /* TODO: bring the interface down, close its device file, drop it from
     * the registry, and free it. */
}

/* ---- Lookup / enumeration ---- */

dmod_dmnetif_api_declaration(1.0, dmnetif_iface_t, _find_by_name, ( const char* name ))
{
    /* TODO: look up by name in the registry. */
    return NULL;
}

dmod_dmnetif_api_declaration(1.0, size_t, _count, ( void ))
{
    /* TODO: return the registry's size. */
    return 0;
}

dmod_dmnetif_api_declaration(1.0, void, _for_each, ( dmnetif_iterator_func_t callback, void* user_data ))
{
    /* TODO: forward to dmlist_foreach() over the registry. */
}

dmod_dmnetif_api_declaration(1.0, const char*, _get_name, ( dmnetif_iface_t iface ))
{
    /* TODO: return iface's stored name. */
    return NULL;
}

/* ---- State control ---- */

dmod_dmnetif_api_declaration(1.0, int, _up, ( dmnetif_iface_t iface ))
{
    /* TODO: DMDRVI_IOCTL_NET_START via the interface's device file. */
    return -ENOSYS;
}

dmod_dmnetif_api_declaration(1.0, int, _down, ( dmnetif_iface_t iface ))
{
    /* TODO: DMDRVI_IOCTL_NET_STOP via the interface's device file. */
    return -ENOSYS;
}

dmod_dmnetif_api_declaration(1.0, bool, _is_up, ( dmnetif_iface_t iface ))
{
    /* TODO: return iface's cached up/down state. */
    return false;
}

dmod_dmnetif_api_declaration(1.0, dmnetif_link_status_t, _get_link_status, ( dmnetif_iface_t iface ))
{
    /* TODO: DMDRVI_IOCTL_NET_GET_LINK_STATUS via the interface's device file. */
    return dmnetif_link_down;
}

/* ---- MAC address ---- */

dmod_dmnetif_api_declaration(1.0, int, _get_mac_address, ( dmnetif_iface_t iface, dmnetif_mac_addr_t* mac ))
{
    /* TODO: DMDRVI_IOCTL_NET_GET_MAC_ADDR via the interface's device file. */
    return -ENOSYS;
}

dmod_dmnetif_api_declaration(1.0, int, _set_mac_address, ( dmnetif_iface_t iface, const dmnetif_mac_addr_t* mac ))
{
    /* TODO: DMDRVI_IOCTL_NET_SET_MAC_ADDR via the interface's device file. */
    return -ENOSYS;
}

/* ---- Frame I/O ---- */

dmod_dmnetif_api_declaration(1.0, size_t, _send, ( dmnetif_iface_t iface, const void* frame, size_t length ))
{
    /* TODO: Dmod_FileWrite() to the interface's device file. */
    return 0;
}

dmod_dmnetif_api_declaration(1.0, size_t, _receive, ( dmnetif_iface_t iface, void* buffer, size_t size ))
{
    /* TODO: Dmod_FileRead() from the interface's device file. */
    return 0;
}

/* ---- Escape hatch ---- */

dmod_dmnetif_api_declaration(1.0, int, _ioctl, ( dmnetif_iface_t iface, int command, void* arg ))
{
    /* TODO: Dmod_Ioctl() on the interface's device file. */
    return -ENOSYS;
}
