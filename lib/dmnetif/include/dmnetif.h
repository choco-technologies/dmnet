#ifndef DMNETIF_H
#define DMNETIF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmnetif_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file dmnetif.h
 * @brief DMOD Network Interface Manager - Public API
 *
 * dmnetif is the boundary between devfs (dmdevfs/dmdrvi device files, e.g.
 * "/dev/dmeth0") and the network stack. A driver (dmeth, and later others -
 * loopback, PPP, Wi-Fi, ...) registers the devfs path it was assigned as a
 * named network interface ("eth0"); everything above that line (a TCP/IP
 * stack, `netctl`/`ifconfig`) only ever talks to dmnetif by interface name
 * and never has to open a device file, know a dmdrvi ioctl command, or
 * depend on dmdrvi/dmdevfs headers at all.
 *
 * dmnetif itself opens the devfs path once at dmnetif_register() time
 * (through the same Dmod_FileOpen/_Ioctl SAL any application would use) and
 * keeps that file handle for the interface's lifetime - callers never see
 * it.
 *
 * There is exactly one dmnetif registry per system (like dmdevfs's own
 * mount registry) - functions here are plain Built-in API
 * (dmod_dmnetif_api), not a DIF/MAL, since there is only ever one manager
 * to call into.
 */

/**
 * @brief Opaque handle to one registered network interface
 */
typedef struct dmnetif_iface* dmnetif_iface_t;

/**
 * @brief Length in bytes of a MAC address (mirrors DMDRVI_NET_MAC_ADDR_LEN -
 *        kept as its own type so callers never need dmdrvi_ioctl.h)
 */
#define DMNETIF_MAC_ADDR_LEN 6

/**
 * @brief MAC address type
 */
typedef struct
{
    uint8_t addr[DMNETIF_MAC_ADDR_LEN];
} dmnetif_mac_addr_t;

/**
 * @brief Maximum length of an interface name (e.g. "eth0"), excluding the
 *        null terminator
 */
#define DMNETIF_NAME_MAX_LEN 15

/**
 * @brief Link status of a network interface
 */
typedef enum
{
    dmnetif_link_down = 0,    /**< Link is down (cable/association lost, or interface not started) */
    dmnetif_link_up   = 1,    /**< Link is up */
} dmnetif_link_status_t;

/* ============================================================================
 *                      Registration (driver-facing)
 * ========================================================================== */

/**
 * @brief Register a devfs-backed device as a named network interface
 *
 * Called by a network driver once its device file's absolute path is known
 * (e.g. from dmdrvi's dmdrvi_path_ready() callback) - not from the driver's
 * dmod_init(), since dmdevfs has not necessarily created the device node
 * yet at that point.
 *
 * dmnetif opens device_path itself and keeps it open until
 * dmnetif_unregister() is called; the driver does not need to (and should
 * not) open it independently.
 *
 * @param name          Interface name (e.g. "eth0"), max DMNETIF_NAME_MAX_LEN
 *                      bytes. Copied internally. Must be unique among
 *                      currently registered interfaces.
 * @param device_path   Absolute devfs path backing this interface (e.g.
 *                      "/dev/dmeth0"). Copied internally.
 *
 * @return Handle to the registered interface, or NULL on failure (name
 *         already in use, name too long, device_path could not be opened,
 *         or out of memory).
 */
dmod_dmnetif_api(1.0, dmnetif_iface_t, _register, ( const char* name, const char* device_path ));

/**
 * @brief Unregister a network interface
 *
 * Brings the interface down first if it is still up, then closes its
 * underlying device file. Safe to call with NULL.
 *
 * @param iface Interface to unregister
 */
dmod_dmnetif_api(1.0, void, _unregister, ( dmnetif_iface_t iface ));

/* ============================================================================
 *                      Lookup / enumeration (consumer-facing)
 * ========================================================================== */

/**
 * @brief Find a registered interface by name
 *
 * @param name Interface name to look up
 *
 * @return Handle to the interface, or NULL if no interface with that name
 *         is currently registered
 */
dmod_dmnetif_api(1.0, dmnetif_iface_t, _find_by_name, ( const char* name ));

/**
 * @brief Number of currently registered interfaces
 */
dmod_dmnetif_api(1.0, size_t, _count, ( void ));

/**
 * @brief Visitor callback for dmnetif_for_each()
 *
 * @param iface     One registered interface
 * @param user_data Passed through from dmnetif_for_each() unchanged
 *
 * @return true to continue iterating, false to stop early
 */
typedef bool (*dmnetif_iterator_func_t)( dmnetif_iface_t iface, void* user_data );

/**
 * @brief Visit every currently registered interface
 *
 * For listing every interface (e.g. "ifconfig -a") without the instability
 * of an index-based lookup across intervening register/unregister calls -
 * mirrors dmlist_foreach(), which dmnetif uses internally to hold its
 * registry. Visits interfaces in registration order.
 *
 * The registry is locked for the duration of the traversal: do not call
 * dmnetif_register()/dmnetif_unregister() (on any interface, including the
 * one currently being visited) from within callback - the underlying
 * dmlist_foreach() advances to the next node after the callback returns,
 * so removing the node currently being visited would leave it dangling,
 * and re-locking the registry's mutex from the same thread would deadlock.
 *
 * @param callback  Called once per interface until it returns false or
 *                  every interface has been visited
 * @param user_data Passed through to callback unchanged
 */
dmod_dmnetif_api(1.0, void, _for_each, ( dmnetif_iterator_func_t callback, void* user_data ));

/**
 * @brief Get an interface's name
 *
 * @param iface Interface handle
 *
 * @return The name passed to dmnetif_register(), or NULL if iface is invalid
 */
dmod_dmnetif_api(1.0, const char*, _get_name, ( dmnetif_iface_t iface ));

/* ============================================================================
 *                      State control
 * ========================================================================== */

/**
 * @brief Bring an interface up (starts packet reception/transmission)
 *
 * @param iface Interface handle
 *
 * @return 0 on success, negative errno on failure
 */
dmod_dmnetif_api(1.0, int, _up, ( dmnetif_iface_t iface ));

/**
 * @brief Bring an interface down
 *
 * @param iface Interface handle
 *
 * @return 0 on success, negative errno on failure
 */
dmod_dmnetif_api(1.0, int, _down, ( dmnetif_iface_t iface ));

/**
 * @brief Check whether dmnetif_up() has been called without a matching
 *        dmnetif_down() since
 *
 * @param iface Interface handle
 *
 * @return true if the interface is up
 */
dmod_dmnetif_api(1.0, bool, _is_up, ( dmnetif_iface_t iface ));

/**
 * @brief Query the interface's current link status from its driver
 *
 * @param iface Interface handle
 *
 * @return Current link status (dmnetif_link_down if iface is invalid)
 */
dmod_dmnetif_api(1.0, dmnetif_link_status_t, _get_link_status, ( dmnetif_iface_t iface ));

/* ============================================================================
 *                      MAC address
 * ========================================================================== */

/**
 * @brief Get an interface's MAC address
 *
 * @param iface Interface handle
 * @param mac   Output buffer for the MAC address
 *
 * @return 0 on success, negative errno on failure
 */
dmod_dmnetif_api(1.0, int, _get_mac_address, ( dmnetif_iface_t iface, dmnetif_mac_addr_t* mac ));

/**
 * @brief Set an interface's MAC address
 *
 * @param iface Interface handle
 * @param mac   MAC address to set
 *
 * @return 0 on success, negative errno on failure
 */
dmod_dmnetif_api(1.0, int, _set_mac_address, ( dmnetif_iface_t iface, const dmnetif_mac_addr_t* mac ));

/* ============================================================================
 *                      Frame I/O (bridge to the network stack)
 * ========================================================================== */

/**
 * @brief Transmit one frame
 *
 * One call transmits exactly one Ethernet-style frame - same one-call/
 * one-frame contract as the underlying dmdrvi driver's write().
 *
 * @param iface  Interface handle
 * @param frame  Raw frame bytes (destination MAC, source MAC, ethertype,
 *               payload - dmnetif never touches the header)
 * @param length Frame length in bytes
 *
 * @return Bytes actually sent, or 0 if the interface is down or the driver
 *         could not accept the frame
 */
dmod_dmnetif_api(1.0, size_t, _send, ( dmnetif_iface_t iface, const void* frame, size_t length ));

/**
 * @brief Receive one frame, if any is available
 *
 * Non-blocking: returns 0 immediately if no frame is currently pending,
 * mirroring the underlying driver's read() semantics. Callers that need to
 * wait for data (e.g. a network stack's RX task) should poll on their own
 * schedule.
 *
 * @param iface  Interface handle
 * @param buffer Buffer to receive the frame into
 * @param size   Size of buffer in bytes
 *
 * @return Bytes actually received, or 0 if none were available
 */
dmod_dmnetif_api(1.0, size_t, _receive, ( dmnetif_iface_t iface, void* buffer, size_t size ));

/* ============================================================================
 *                      Escape hatch
 * ========================================================================== */

/**
 * @brief Forward a raw ioctl to the interface's underlying device file
 *
 * Covers driver-specific commands beyond the generic control surface above
 * (e.g. dmeth's DMETH_IOCTL_SET_PROMISCUOUS_MODE). The caller must know
 * which driver backs the interface to interpret command/arg correctly -
 * unlike every other dmnetif function, this one intentionally punches
 * through the devfs/network boundary, the same way a raw ioctl() punches
 * through a POSIX file descriptor.
 *
 * @param iface   Interface handle
 * @param command Driver-specific ioctl command
 * @param arg     Command-specific argument
 *
 * @return 0 on success, negative errno on failure
 */
dmod_dmnetif_api(1.0, int, _ioctl, ( dmnetif_iface_t iface, int command, void* arg ));

#ifdef __cplusplus
}
#endif

#endif // DMNETIF_H
