/**
 * @file dmroute.c
 * @brief DMOD IP Routing Table - Implementation
 *
 * Table of routes (destination/netmask/gateway/iface_name/metric/origin)
 * backed by dmlist, guarded by a single mutex - same registry shape as
 * dmnetif.c's own g_ifaces. See dmroute.h and docs/dmroute.md for the full
 * rationale, including why this module has no dependency on dmnetif at
 * all (dmnetif depends on *this* module instead, calling dmroute_add()/
 * _remove() directly from dmnetif_set_ip_address() - see
 * lib/dmnetif/src/dmnetif.c).
 */
#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmroute.h"
#include "dmlist.h"
#include "dmosi.h"
#include <string.h>
#include <errno.h>

/**
 * @brief Magic value guarding struct dmroute_entry against stale/invalid
 *        pointers - "DRTE" packed into a uint32_t
 */
#define DMROUTE_CONTEXT_MAGIC    0x44525445u

/**
 * @brief Private definition of the opaque dmroute_route_t handle
 */
struct dmroute_entry
{
    uint32_t                 magic;                                /**< Must equal DMROUTE_CONTEXT_MAGIC for this struct to be considered valid - see is_valid_route() */
    dmroute_addr_t               destination;                           /**< Destination network address, already masked with netmask */
    dmroute_addr_t               netmask;                                /**< Netmask for the destination network */
    dmroute_addr_t               gateway;                                 /**< Next-hop gateway; family is dmroute_family_none for a directly-connected/on-link route */
    char                      iface_name[DMROUTE_IFACE_NAME_MAX_LEN + 1]; /**< Name of the egress interface (never resolved or validated by dmroute itself) */
    uint32_t                  metric;                                   /**< Tie-breaker among equally-specific matches - lower wins */
    dmroute_origin_t          origin;                                    /**< How this route came to exist */
};

/**
 * @brief Table of every currently configured route (struct dmroute_entry*)
 *
 * Created in dmod_init(), destroyed in dmod_deinit(). Guarded by g_mutex.
 */
static dmlist_context_t* g_routes = NULL;

/**
 * @brief Mutex guarding g_routes against concurrent add/remove/lookup calls
 */
static dmosi_mutex_t g_mutex = NULL;

/**
 * @brief Check whether a handle is a live, correctly-typed dmroute_route_t
 *
 * @param route Handle to validate (NULL is a valid input - returns false)
 *
 * @return true if route is non-NULL and its magic field is intact
 */
static bool is_valid_route(dmroute_route_t route)
{
    return route != NULL && route->magic == DMROUTE_CONTEXT_MAGIC;
}

/**
 * @brief dmlist_compare_func_t comparing two elements by pointer identity
 *
 * Used by dmroute_remove() to remove a specific route from the table by
 * its own address, regardless of its contents.
 */
static int compare_pointer(const void* data, const void* user_data)
{
    return (data == user_data) ? 0 : -1;
}

/**
 * @brief Check whether an IP family value is a real address family (not "none")
 *
 * A route's destination/netmask must always be an actual address - unlike
 * a gateway, "none" is never a valid destination/netmask for an entry
 * that exists at all.
 *
 * @param family Value to check
 *
 * @return true if family is dmroute_family_v4 or _v6
 */
static bool is_valid_ip_family(dmroute_family_t family)
{
    return family == dmroute_family_v4 || family == dmroute_family_v6;
}

/**
 * @brief Number of address bytes significant for a given family
 *
 * @param family Address family
 *
 * @return DMROUTE_IPV4_ADDR_LEN, DMROUTE_IPV6_ADDR_LEN, or 0 for dmroute_family_none
 */
static size_t addr_byte_length(dmroute_family_t family)
{
    switch (family)
    {
        case dmroute_family_v4: return DMROUTE_IPV4_ADDR_LEN;
        case dmroute_family_v6: return DMROUTE_IPV6_ADDR_LEN;
        default:             return 0;
    }
}

/**
 * @brief Bitwise-AND an address with a netmask
 *
 * `ip` and `netmask` must be the same family; `out` is written with that
 * family and the masked bytes. `addr.v4`/`addr.v6` share the same leading
 * storage in dmroute_addr_t's union, so indexing through `.addr.v6` up to
 * addr_byte_length(family) bytes is safe regardless of which member was
 * last written.
 */
static void apply_mask(const dmroute_addr_t* ip, const dmroute_addr_t* netmask, dmroute_addr_t* out)
{
    out->family = ip->family;
    size_t length = addr_byte_length(ip->family);
    for (size_t i = 0; i < length; i++)
    {
        out->addr.v6[i] = ip->addr.v6[i] & netmask->addr.v6[i];
    }
}

/**
 * @brief Compare two addresses of the same family for equality
 */
static bool addr_equal(const dmroute_addr_t* a, const dmroute_addr_t* b)
{
    if (a->family != b->family)
        return false;

    size_t length = addr_byte_length(a->family);
    for (size_t i = 0; i < length; i++)
    {
        if (a->addr.v6[i] != b->addr.v6[i])
            return false;
    }
    return true;
}

/**
 * @brief Count the number of set bits in a netmask - used as the
 *        "specificity" of a route for longest-prefix-match comparison
 */
static int netmask_prefix_length(const dmroute_addr_t* netmask)
{
    size_t length = addr_byte_length(netmask->family);
    int bits = 0;
    for (size_t i = 0; i < length; i++)
    {
        uint8_t byte = netmask->addr.v6[i];
        while (byte != 0)
        {
            bits += byte & 1;
            byte >>= 1;
        }
    }
    return bits;
}

/**
 * @brief Shared implementation behind dmod_dmroute_api_declaration(_add) -
 *        validates arguments, masks `destination` with `netmask`, and
 *        inserts the new entry
 *
 * @return The new entry, or NULL on failure
 */
static struct dmroute_entry* add_route(const dmroute_addr_t* destination, const dmroute_addr_t* netmask,
                                        const dmroute_addr_t* gateway, const char* iface_name,
                                        uint32_t metric, dmroute_origin_t origin)
{
    if (destination == NULL || netmask == NULL || iface_name == NULL)
    {
        DMOD_LOG_ERROR("dmroute_add: NULL destination, netmask, or iface_name\n");
        return NULL;
    }

    if (!is_valid_ip_family(destination->family) || netmask->family != destination->family)
    {
        DMOD_LOG_ERROR("dmroute_add: invalid or mismatched destination/netmask family\n");
        return NULL;
    }

    if (gateway != NULL && gateway->family != dmroute_family_none && gateway->family != destination->family)
    {
        DMOD_LOG_ERROR("dmroute_add: gateway family does not match destination\n");
        return NULL;
    }

    struct dmroute_entry* entry = Dmod_Malloc(sizeof(*entry));
    if (entry == NULL)
        return NULL;

    memset(entry, 0, sizeof(*entry));
    entry->magic = DMROUTE_CONTEXT_MAGIC;
    entry->netmask = *netmask;
    apply_mask(destination, netmask, &entry->destination);
    if (gateway != NULL && gateway->family != dmroute_family_none)
    {
        entry->gateway = *gateway;
    }
    Dmod_SnPrintf(entry->iface_name, sizeof(entry->iface_name), "%s", iface_name);
    entry->metric = metric;
    entry->origin = origin;

    dmosi_mutex_lock(g_mutex);
    bool added = dmlist_push_back(g_routes, entry);
    dmosi_mutex_unlock(g_mutex);

    if (!added)
    {
        Dmod_Free(entry);
        return NULL;
    }

    return entry;
}

/* ---- DMOD lifecycle ---- */

/**
 * @brief Module initialization - allocates the route table and its
 *        guarding mutex
 *
 * @param Config Module configuration (unused)
 *
 * @return 0 on success, -1 if the table or mutex could not be allocated
 */
int dmod_init(const Dmod_Config_t *Config)
{
    (void)Config;

    g_routes = dmlist_create(Dmod_GetCurrentAllocatorName());
    g_mutex  = dmosi_mutex_create(false);
    if (g_routes == NULL || g_mutex == NULL)
    {
        DMOD_LOG_ERROR("Failed to allocate dmroute table\n");
        return -1;
    }

    DMOD_LOG_INFO("DMROUTE routing table initialized\n");
    return 0;
}

/**
 * @brief Module deinitialization - frees every remaining route, then the
 *        table and its mutex
 *
 * @return Always 0
 */
int dmod_deinit(void)
{
    size_t count = dmlist_size(g_routes);
    for (size_t i = 0; i < count; i++)
    {
        struct dmroute_entry* entry = (struct dmroute_entry*)dmlist_pop_front(g_routes);
        entry->magic = 0;
        Dmod_Free(entry);
    }
    dmlist_destroy(g_routes);
    g_routes = NULL;

    dmosi_mutex_destroy(g_mutex);
    g_mutex = NULL;

    DMOD_LOG_INFO("DMROUTE routing table deinitialized\n");
    return 0;
}

/* ---- Add / remove ---- */

/**
 * @brief Implementation of dmroute_add() - see dmroute.h
 */
dmod_dmroute_api_declaration(1.0, dmroute_route_t, _add, ( const dmroute_addr_t* destination, const dmroute_addr_t* netmask, const dmroute_addr_t* gateway, const char* iface_name, uint32_t metric, dmroute_origin_t origin ))
{
    return (dmroute_route_t)add_route(destination, netmask, gateway, iface_name, metric, origin);
}

/**
 * @brief Implementation of dmroute_remove() - see dmroute.h
 */
dmod_dmroute_api_declaration(1.0, void, _remove, ( dmroute_route_t route ))
{
    if (!is_valid_route(route))
        return;

    dmosi_mutex_lock(g_mutex);
    bool removed = dmlist_remove(g_routes, route, compare_pointer);
    dmosi_mutex_unlock(g_mutex);

    if (removed)
    {
        route->magic = 0;
        Dmod_Free(route);
    }
}

/* ---- Lookup / enumeration ---- */

/**
 * @brief dmlist_foreach() state used by dmroute_lookup() to track the
 *        best longest-prefix match seen so far
 */
typedef struct
{
    const dmroute_addr_t*    destination;      /**< Address being looked up */
    struct dmroute_entry* best;              /**< Best match found so far, or NULL */
    int                    best_prefix_len;   /**< netmask_prefix_length() of best - only meaningful if best != NULL */
} lookup_ctx_t;

/**
 * @brief dmlist_iterator_func_t comparing one route against the running
 *        best match in a lookup_ctx_t
 */
static bool lookup_visitor(void* data, void* user_data)
{
    struct dmroute_entry* entry = (struct dmroute_entry*)data;
    lookup_ctx_t* ctx = (lookup_ctx_t*)user_data;

    if (entry->destination.family != ctx->destination->family)
        return true;

    dmroute_addr_t masked;
    apply_mask(ctx->destination, &entry->netmask, &masked);
    if (!addr_equal(&masked, &entry->destination))
        return true;

    int prefix_len = netmask_prefix_length(&entry->netmask);
    if (ctx->best == NULL ||
        prefix_len > ctx->best_prefix_len ||
        (prefix_len == ctx->best_prefix_len && entry->metric < ctx->best->metric))
    {
        ctx->best = entry;
        ctx->best_prefix_len = prefix_len;
    }

    return true;
}

/**
 * @brief Implementation of dmroute_lookup() - see dmroute.h
 */
dmod_dmroute_api_declaration(1.0, dmroute_route_t, _lookup, ( const dmroute_addr_t* destination_ip ))
{
    if (destination_ip == NULL || !is_valid_ip_family(destination_ip->family))
        return NULL;

    lookup_ctx_t ctx = { .destination = destination_ip, .best = NULL, .best_prefix_len = -1 };
    dmosi_mutex_lock(g_mutex);
    dmlist_foreach(g_routes, lookup_visitor, &ctx);
    dmosi_mutex_unlock(g_mutex);

    return (dmroute_route_t)ctx.best;
}

/**
 * @brief Implementation of dmroute_count() - see dmroute.h
 */
dmod_dmroute_api_declaration(1.0, size_t, _count, ( void ))
{
    dmosi_mutex_lock(g_mutex);
    size_t count = dmlist_size(g_routes);
    dmosi_mutex_unlock(g_mutex);
    return count;
}

/**
 * @brief Closure passed as dmlist_foreach()'s user_data by
 *        dmroute_for_each(), letting for_each_trampoline() recover the
 *        caller's own callback/user_data pair
 */
typedef struct
{
    dmroute_iterator_func_t callback;    /**< Caller-supplied callback to invoke per route */
    void*                   user_data;   /**< Caller-supplied opaque data, passed through to callback unchanged */
} for_each_ctx_t;

/**
 * @brief dmlist_iterator_func_t adapting dmlist's "void* data" element type
 *        to dmroute_for_each()'s "dmroute_route_t route" callback signature
 */
static bool for_each_trampoline(void* data, void* user_data)
{
    for_each_ctx_t* ctx = (for_each_ctx_t*)user_data;
    return ctx->callback((dmroute_route_t)data, ctx->user_data);
}

/**
 * @brief Implementation of dmroute_for_each() - see dmroute.h
 */
dmod_dmroute_api_declaration(1.0, void, _for_each, ( dmroute_iterator_func_t callback, void* user_data ))
{
    if (callback == NULL)
        return;

    for_each_ctx_t ctx = { .callback = callback, .user_data = user_data };
    dmosi_mutex_lock(g_mutex);
    dmlist_foreach(g_routes, for_each_trampoline, &ctx);
    dmosi_mutex_unlock(g_mutex);
}

/* ---- Accessors ---- */

/**
 * @brief Implementation of dmroute_get_destination() - see dmroute.h
 */
dmod_dmroute_api_declaration(1.0, int, _get_destination, ( dmroute_route_t route, dmroute_addr_t* destination ))
{
    if (!is_valid_route(route) || destination == NULL)
        return -EINVAL;

    *destination = route->destination;
    return 0;
}

/**
 * @brief Implementation of dmroute_get_netmask() - see dmroute.h
 */
dmod_dmroute_api_declaration(1.0, int, _get_netmask, ( dmroute_route_t route, dmroute_addr_t* netmask ))
{
    if (!is_valid_route(route) || netmask == NULL)
        return -EINVAL;

    *netmask = route->netmask;
    return 0;
}

/**
 * @brief Implementation of dmroute_get_gateway() - see dmroute.h
 */
dmod_dmroute_api_declaration(1.0, int, _get_gateway, ( dmroute_route_t route, dmroute_addr_t* gateway ))
{
    if (!is_valid_route(route) || gateway == NULL)
        return -EINVAL;

    *gateway = route->gateway;
    return 0;
}

/**
 * @brief Implementation of dmroute_get_iface_name() - see dmroute.h
 */
dmod_dmroute_api_declaration(1.0, const char*, _get_iface_name, ( dmroute_route_t route ))
{
    return is_valid_route(route) ? route->iface_name : NULL;
}

/**
 * @brief Implementation of dmroute_get_metric() - see dmroute.h
 */
dmod_dmroute_api_declaration(1.0, uint32_t, _get_metric, ( dmroute_route_t route ))
{
    return is_valid_route(route) ? route->metric : 0;
}

/**
 * @brief Implementation of dmroute_get_origin() - see dmroute.h
 */
dmod_dmroute_api_declaration(1.0, dmroute_origin_t, _get_origin, ( dmroute_route_t route ))
{
    return is_valid_route(route) ? route->origin : dmroute_origin_static;
}
