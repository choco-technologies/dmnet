/**
 * @file networkd.c
 * @brief networkd - runs dmnetbridge_handle_netif_rx() for one interface
 *
 * One instance per interface, not one service for all of them. dmnetif
 * reports every interface it registers as a `netif` class device, a device
 * rule (networkd.rules) maps that class to this module's unit template, and
 * libsystemd instantiates `networkd@<interface>` for each one - the same
 * shape dmtty and console@.ini use for tty nodes. The interface's name
 * arrives as argv[1], from the template's `%i`.
 *
 * The pump runs on this process's own stack rather than on a thread spawned
 * beside it. There is exactly one interface to pump and nothing for the
 * process to do afterwards, so a supervisor thread would only sit in a sleep
 * loop waiting to be killed - an extra stack per instance, bought for
 * nothing. dmnetbridge_handle_netif_rx() blocks until its interface is gone,
 * which is precisely this process's lifetime.
 *
 * It also removes the old shape's scope limit: interfaces were enumerated
 * once at startup, so anything registered later never got a pump. Here a
 * late interface is just another device notification, and gets its own
 * instance like any other.
 */
#include "dmod.h"
#include "dmnetif.h"
#include "dmnetbridge.h"
#include <errno.h>

/**
 * @brief Main function of the application
 *
 * @param argc Argument count
 * @param argv argv[1] is the interface name to pump (the unit template's %i)
 *
 * @return 0 once the interface is gone, -EINVAL without an interface name,
 *         -ENODEV if no interface by that name is registered
 */
int main(int argc, char *argv[])
{
    if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0')
    {
        DMOD_LOG_ERROR("networkd: no interface name given\n");
        Dmod_Printf("Usage: networkd <interface>\n");
        Dmod_Printf("Started per interface from networkd@.ini - see networkd.rules\n");
        return -EINVAL;
    }

    const char* name = argv[1];

    dmnetif_iface_t iface = dmnetif_find_by_name(name);
    if (iface == NULL)
    {
        DMOD_LOG_ERROR("networkd: no interface named '%s' is registered\n", name);
        return -ENODEV;
    }

    /* An instance that was killed rather than allowed to finish leaves its
     * interface marked as being pumped, and this one would then find it taken
     * and return immediately - so release it first. Per interface, never the
     * global dmnetbridge_reset(): every other interface has its own instance
     * and its own bookkeeping to keep. */
    dmnetbridge_release_netif(iface);

    DMOD_LOG_INFO("networkd: pumping '%s'\n", name);

    /* Returns when the interface is gone - see dmnetbridge_handle_netif_rx() */
    dmnetbridge_handle_netif_rx(iface);

    DMOD_LOG_INFO("networkd: '%s' is gone, stopping\n", name);
    return 0;
}
