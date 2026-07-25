#ifndef DMROUTE_H
#define DMROUTE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmip.h"
#include "dmroute_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file dmroute.h
 * @brief DMOD IP Routing Table - Public API
 *
 * dmroute is a longest-prefix-match routing table: each entry maps a
 * destination network (address + netmask) to an egress interface - by
 * name (a plain string dmroute never resolves or validates itself), plus
 * an optional gateway and a metric used to break ties between otherwise
 * equally-specific matches.
 *
 * dmroute depends only on dmip (for dmip_addr_t) - never on dmnetif or any
 * other module. This is the opposite direction from what "routing sits on
 * top of the interface manager" might suggest: dmnetif is the one that
 * depends on and calls *this* module (dmnetif_set_ip_address() calls
 * dmroute_add()/_remove() directly to keep an interface's
 * directly-connected route in sync - see lib/dmnetif/src/dmnetif.c), not
 * the other way around. Keeping dmroute itself free of any dependency
 * back on dmnetif is what makes that possible without a build cycle - see
 * docs/dmroute.md for the full rationale, including why this is a plain,
 * always-connected dependency like any other (dmip, dmlist, dmosi, ...)
 * rather than an optional/pluggable DIF or MAL connection: dmnetif always
 * needs a routing table.
 *
 * dmroute never sends or receives a frame itself - it only answers "which
 * interface (and gateway, if any) should this destination go through" for
 * whatever runs above it (a TCP/IP stack, the `ip` CLI) to act on.
 *
 * There is exactly one routing table per system - functions here are
 * plain Built-in API (dmod_dmroute_api), not a DIF/MAL, since there is
 * only ever one table to call into.
 */

/**
 * @brief Opaque handle to one routing table entry
 */
typedef struct dmroute_entry* dmroute_route_t;

/**
 * @brief Maximum length of an interface name, excluding the null
 *        terminator - matches dmnetif's DMNETIF_NAME_MAX_LEN, but defined
 *        independently since dmroute does not depend on dmnetif (a name
 *        longer than this is simply truncated on dmroute_add(), the same
 *        way dmnetif truncates a name it can't fit - it's dmroute's
 *        callers, e.g. dmnetif itself, that are responsible for the name
 *        actually meaning something)
 */
#define DMROUTE_IFACE_NAME_MAX_LEN 15

/**
 * @brief How a route entry came to exist
 */
typedef enum
{
    dmroute_origin_static    = 0,    /**< Added by an explicit caller (e.g. `ip route add`) */
    dmroute_origin_connected = 1,    /**< Added by dmnetif because an interface was assigned an IP address */
} dmroute_origin_t;

/**
 * @brief Metric used for connected routes and, by default, anything added
 *        without an explicit metric - the lowest value, so a
 *        directly-connected route always wins over a static one to the
 *        same destination
 */
#define DMROUTE_DEFAULT_METRIC 0

/* ============================================================================
 *                      Add / remove
 * ========================================================================== */

/**
 * @brief Add a route to the table
 *
 * `destination` does not need to already be masked with `netmask` -
 * dmroute masks it internally before storing the entry, so
 * `dmroute_add(&addr, &netmask, ...)` behaves the same whether `addr` is a
 * network address or a specific host address within that network.
 *
 * dmroute does not check that `iface_name` refers to a real, currently
 * registered interface - it has no way to (see this header's file
 * comment) - so callers that care (e.g. `ip route add`) must validate
 * that themselves before calling this.
 *
 * @param destination  Destination network address (family must be
 *                      dmip_family_v4 or _v6)
 * @param netmask      Netmask for the destination network (same family as
 *                      destination)
 * @param gateway      Next-hop gateway address, or NULL (or family
 *                      dmip_family_none) for a directly-connected/on-link
 *                      route with no gateway. If given, family must match
 *                      destination.
 * @param iface_name   Name of the interface to route matching traffic
 *                      through - truncated to DMROUTE_IFACE_NAME_MAX_LEN
 *                      bytes if longer
 * @param metric       Priority among routes that match a destination
 *                      equally specifically - lower wins. Use
 *                      DMROUTE_DEFAULT_METRIC if you don't care.
 * @param origin       Whether this route is caller-added or represents an
 *                      interface's own directly-connected subnet - purely
 *                      informational (readable back via
 *                      dmroute_get_origin()), does not affect lookup.
 *                      Callers other than dmnetif itself should always
 *                      pass dmroute_origin_static.
 *
 * @return Handle to the new route, or NULL on failure (invalid/mismatched
 *         families or out of memory)
 */
dmod_dmroute_api(1.0, dmroute_route_t, _add, ( const dmip_addr_t* destination, const dmip_addr_t* netmask, const dmip_addr_t* gateway, const char* iface_name, uint32_t metric, dmroute_origin_t origin ));

/**
 * @brief Remove a route from the table
 *
 * Safe to call with NULL.
 *
 * @param route Route to remove
 */
dmod_dmroute_api(1.0, void, _remove, ( dmroute_route_t route ));

/* ============================================================================
 *                      Lookup / enumeration
 * ========================================================================== */

/**
 * @brief Find the best-matching route for a destination address
 *
 * Longest-prefix match: among every route whose (destination & netmask)
 * equals (destination_ip & that route's netmask), the one with the most
 * netmask bits set wins; ties are broken by the lowest metric, then by
 * whichever route was added first.
 *
 * @param destination_ip Address to look up (family must be dmip_family_v4
 *                        or _v6)
 *
 * @return The best-matching route, or NULL if no route matches (no
 *         default route present)
 */
dmod_dmroute_api(1.0, dmroute_route_t, _lookup, ( const dmip_addr_t* destination_ip ));

/**
 * @brief Number of routes currently in the table
 */
dmod_dmroute_api(1.0, size_t, _count, ( void ));

/**
 * @brief Visitor callback for dmroute_for_each()
 *
 * @param route     One route in the table
 * @param user_data Passed through from dmroute_for_each() unchanged
 *
 * @return true to continue iterating, false to stop early
 */
typedef bool (*dmroute_iterator_func_t)( dmroute_route_t route, void* user_data );

/**
 * @brief Visit every route currently in the table
 *
 * For listing the whole table (`ip route show`) without the instability
 * of an index-based lookup across intervening add/remove calls - mirrors
 * dmnetif_for_each(). Visits routes in the order they were added. Do not
 * call dmroute_add()/dmroute_remove() from within callback - the
 * underlying traversal advances to the next node after the callback
 * returns, so removing the node currently being visited would leave it
 * dangling.
 *
 * @param callback  Called once per route until it returns false or every
 *                  route has been visited
 * @param user_data Passed through to callback unchanged
 */
dmod_dmroute_api(1.0, void, _for_each, ( dmroute_iterator_func_t callback, void* user_data ));

/* ============================================================================
 *                      Accessors
 * ========================================================================== */

/**
 * @brief Get a route's destination network address (already masked with
 *        its netmask)
 *
 * @param route       Route handle
 * @param destination Output buffer
 *
 * @return 0 on success, negative errno on failure (invalid route or NULL destination)
 */
dmod_dmroute_api(1.0, int, _get_destination, ( dmroute_route_t route, dmip_addr_t* destination ));

/**
 * @brief Get a route's netmask
 *
 * @param route   Route handle
 * @param netmask Output buffer
 *
 * @return 0 on success, negative errno on failure (invalid route or NULL netmask)
 */
dmod_dmroute_api(1.0, int, _get_netmask, ( dmroute_route_t route, dmip_addr_t* netmask ));

/**
 * @brief Get a route's gateway
 *
 * @param route   Route handle
 * @param gateway Output buffer. On success, gateway->family is
 *                dmip_family_none for a directly-connected/on-link route
 *                with no gateway.
 *
 * @return 0 on success, negative errno on failure (invalid route or NULL gateway)
 */
dmod_dmroute_api(1.0, int, _get_gateway, ( dmroute_route_t route, dmip_addr_t* gateway ));

/**
 * @brief Get the name of the interface a route sends matching traffic through
 *
 * @param route Route handle
 *
 * @return Interface name, or NULL if route is invalid. dmroute never
 *         validated it against any interface registry to begin with (see
 *         this header's file comment), so it's not guaranteed to name a
 *         currently existing interface either.
 */
dmod_dmroute_api(1.0, const char*, _get_iface_name, ( dmroute_route_t route ));

/**
 * @brief Get a route's metric
 *
 * @param route Route handle
 *
 * @return The route's metric, or 0 if route is invalid
 */
dmod_dmroute_api(1.0, uint32_t, _get_metric, ( dmroute_route_t route ));

/**
 * @brief Get how a route came to exist
 *
 * @param route Route handle
 *
 * @return dmroute_origin_static if route is invalid
 */
dmod_dmroute_api(1.0, dmroute_origin_t, _get_origin, ( dmroute_route_t route ));

#ifdef __cplusplus
}
#endif

#endif // DMROUTE_H
