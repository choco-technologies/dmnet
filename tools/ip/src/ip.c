/**
 * @file ip.c
 * @brief ip - inspect and control dmroute's IP routing table
 *
 * A thin CLI over dmroute's API (`ip route ...`), the way ifconfig is a
 * thin CLI over dmnetif's - never touches dmroute's dmlist/mutex
 * internals, only its public add/remove/lookup/for_each surface. Follows
 * Linux `iproute2`'s `ip route` subset closely enough to be familiar, but
 * only covers what dmroute actually tracks (IPv4 only - dmroute itself is
 * family-agnostic, but there is no IPv6 text parser here yet, same
 * limitation ifconfig has for `create`/`broadcast`).
 */
#include "dmod.h"
#include "dmroute.h"
#include "dmnetif.h"
#include <string.h>

/* Parses a plain decimal uint32_t (dmod's minimal module runtime has no
 * strtol()/atoi() - see dmod/src/module/string.c's replacement set). */
static bool parse_uint32(const char* s, uint32_t* out)
{
    if (s == NULL || *s == '\0')
        return false;

    uint32_t value = 0;
    for (const char* p = s; *p != '\0'; p++)
    {
        if (*p < '0' || *p > '9')
            return false;

        if (value > (UINT32_MAX - (uint32_t)(*p - '0')) / 10)
            return false;

        value = value * 10 + (uint32_t)(*p - '0');
    }

    *out = value;
    return true;
}

/* Parses "A.B.C.D" (each octet 0-255) into an IPv4 dmroute_addr_t. */
static bool parse_ipv4_address(const char* s, dmroute_addr_t* ip)
{
    if (s == NULL)
        return false;

    ip->family = dmroute_family_v4;

    for (int i = 0; i < DMROUTE_IPV4_ADDR_LEN; i++)
    {
        if (*s < '0' || *s > '9')
            return false;

        uint32_t octet = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9')
        {
            octet = octet * 10 + (uint32_t)(*s - '0');
            if (octet > 255 || ++digits > 3)
                return false;
            s++;
        }
        ip->addr.v4[i] = (uint8_t)octet;

        if (i < DMROUTE_IPV4_ADDR_LEN - 1)
        {
            if (*s != '.')
                return false;
            s++;
        }
    }

    return *s == '\0';
}

static void prefix_len_to_netmask(uint8_t prefix_len, dmroute_addr_t* netmask)
{
    netmask->family = dmroute_family_v4;
    for (int i = 0; i < DMROUTE_IPV4_ADDR_LEN; i++)
    {
        int bits_left = (int)prefix_len - (i * 8);
        if (bits_left >= 8)
            netmask->addr.v4[i] = 0xFF;
        else if (bits_left <= 0)
            netmask->addr.v4[i] = 0x00;
        else
            netmask->addr.v4[i] = (uint8_t)(0xFF << (8 - bits_left));
    }
}

static int netmask_to_prefix_len(const dmroute_addr_t* netmask)
{
    int bits = 0;
    for (int i = 0; i < DMROUTE_IPV4_ADDR_LEN; i++)
    {
        uint8_t byte = netmask->addr.v4[i];
        while (byte != 0)
        {
            bits += byte & 1;
            byte >>= 1;
        }
    }
    return bits;
}

/* Parses "A.B.C.D", "A.B.C.D/N", or "default" (= 0.0.0.0/0) into a
 * destination address and netmask. A bare address without "/N" is a host
 * route (netmask /32) - same default `ip route add <addr> dev <iface>`
 * uses. */
static bool parse_destination(const char* s, dmroute_addr_t* dest, dmroute_addr_t* netmask)
{
    if (s == NULL)
        return false;

    if (strcmp(s, "default") == 0)
    {
        dest->family = dmroute_family_v4;
        memset(dest->addr.v4, 0, DMROUTE_IPV4_ADDR_LEN);
        prefix_len_to_netmask(0, netmask);
        return true;
    }

    char addr_part[16];
    const char* slash = strchr(s, '/');
    size_t addr_len = (slash != NULL) ? (size_t)(slash - s) : strlen(s);
    if (addr_len == 0 || addr_len >= sizeof(addr_part))
        return false;

    memcpy(addr_part, s, addr_len);
    addr_part[addr_len] = '\0';
    if (!parse_ipv4_address(addr_part, dest))
        return false;

    if (slash == NULL)
    {
        prefix_len_to_netmask(32, netmask);
        return true;
    }

    uint32_t prefix_len = 0;
    if (!parse_uint32(slash + 1, &prefix_len) || prefix_len > 32)
        return false;

    prefix_len_to_netmask((uint8_t)prefix_len, netmask);
    return true;
}

static void print_ipv4_address(const dmroute_addr_t* ip)
{
    Dmod_Printf("%u.%u.%u.%u", ip->addr.v4[0], ip->addr.v4[1], ip->addr.v4[2], ip->addr.v4[3]);
}

static void print_route(dmroute_route_t route)
{
    dmroute_addr_t destination = { 0 };
    dmroute_addr_t netmask = { 0 };
    dmroute_addr_t gateway = { 0 };
    dmroute_get_destination(route, &destination);
    dmroute_get_netmask(route, &netmask);
    dmroute_get_gateway(route, &gateway);

    if (destination.family != dmroute_family_v4)
    {
        /* dmroute itself is family-agnostic (e.g. an interface could get
         * an IPv6 address and pick up a v6 connected route), but this CLI
         * has no IPv6 text representation yet - say so rather than
         * printing four bytes of a v6 address as if they were an IPv4
         * quad. */
        const char* iface_name = dmroute_get_iface_name(route);
        Dmod_Printf("(unsupported address family) dev %s metric %u\n",
                    (iface_name != NULL) ? iface_name : "?", dmroute_get_metric(route));
        return;
    }

    if (destination.addr.v4[0] == 0 && destination.addr.v4[1] == 0 &&
        destination.addr.v4[2] == 0 && destination.addr.v4[3] == 0 &&
        netmask_to_prefix_len(&netmask) == 0)
    {
        Dmod_Printf("default");
    }
    else
    {
        print_ipv4_address(&destination);
        Dmod_Printf("/%d", netmask_to_prefix_len(&netmask));
    }

    if (gateway.family != dmroute_family_none)
    {
        Dmod_Printf(" via ");
        print_ipv4_address(&gateway);
    }

    const char* iface_name = dmroute_get_iface_name(route);
    Dmod_Printf(" dev %s metric %u", (iface_name != NULL) ? iface_name : "?", dmroute_get_metric(route));

    if (dmroute_get_origin(route) == dmroute_origin_connected)
    {
        Dmod_Printf(" connected");
    }

    Dmod_Printf("\n");
}

static bool print_route_visitor(dmroute_route_t route, void* user_data)
{
    (void)user_data;
    print_route(route);
    return true;
}

static void print_usage(const char* prog)
{
    Dmod_Printf("Usage:\n");
    Dmod_Printf("  %s route [show]                                          List all routes\n", prog);
    Dmod_Printf("  %s route show <dest>                                     Show the best-matching route for <dest>\n", prog);
    Dmod_Printf("  %s route get <dest>                                      Same as 'route show <dest>'\n", prog);
    Dmod_Printf("  %s route add <dest>[/<prefixlen>] [via <gw>] dev <iface> [metric <n>]  Add a route\n", prog);
    Dmod_Printf("  %s route del <dest>[/<prefixlen>] dev <iface>            Remove a route\n", prog);
    Dmod_Printf("  %s --help | -h                                           Show this help\n", prog);
    Dmod_Printf("\n<dest> is an IPv4 address, \"A.B.C.D/N\" CIDR notation, or \"default\".\n");
}

static int cmd_show_one(const char* prog, const char* dest_str)
{
    dmroute_addr_t dest = { 0 };
    dmroute_addr_t unused_mask = { 0 };
    if (!parse_destination(dest_str, &dest, &unused_mask))
    {
        Dmod_Printf("%s: invalid destination '%s'\n", prog, dest_str);
        return 1;
    }

    dmroute_route_t route = dmroute_lookup(&dest);
    if (route == NULL)
    {
        Dmod_Printf("%s: no route to %s\n", prog, dest_str);
        return 1;
    }

    print_route(route);
    return 0;
}

static int cmd_route_add(const char* prog, int argc, char* argv[])
{
    /* argv[0] == "add", argv[1] == "<dest>[/<prefixlen>]" */
    if (argc < 2)
    {
        print_usage(prog);
        return 1;
    }

    dmroute_addr_t destination = { 0 };
    dmroute_addr_t netmask = { 0 };
    if (!parse_destination(argv[1], &destination, &netmask))
    {
        Dmod_Printf("%s: invalid destination '%s'\n", prog, argv[1]);
        return 1;
    }

    dmroute_addr_t gateway = { 0 };
    bool have_gateway = false;
    const char* iface_name = NULL;
    uint32_t metric = DMROUTE_DEFAULT_METRIC;

    for (int i = 2; i < argc; )
    {
        if (strcmp(argv[i], "via") == 0 && i + 1 < argc)
        {
            if (!parse_ipv4_address(argv[i + 1], &gateway))
            {
                Dmod_Printf("%s: invalid gateway '%s'\n", prog, argv[i + 1]);
                return 1;
            }
            have_gateway = true;
            i += 2;
        }
        else if (strcmp(argv[i], "dev") == 0 && i + 1 < argc)
        {
            iface_name = argv[i + 1];
            i += 2;
        }
        else if (strcmp(argv[i], "metric") == 0 && i + 1 < argc)
        {
            if (!parse_uint32(argv[i + 1], &metric))
            {
                Dmod_Printf("%s: invalid metric '%s'\n", prog, argv[i + 1]);
                return 1;
            }
            i += 2;
        }
        else
        {
            print_usage(prog);
            return 1;
        }
    }

    if (iface_name == NULL)
    {
        Dmod_Printf("%s: 'dev <iface>' is required\n", prog);
        return 1;
    }

    /* dmroute itself never validates iface_name (see dmroute.h's file
     * comment) - it has no dependency on dmnetif to do that with, so this
     * CLI checks it here instead, the same way a real `ip route add`
     * rejects an unknown device. */
    if (dmnetif_find_by_name(iface_name) == NULL)
    {
        Dmod_Printf("%s: unknown interface '%s'\n", prog, iface_name);
        return 1;
    }

    dmroute_route_t route = dmroute_add(&destination, &netmask, have_gateway ? &gateway : NULL, iface_name, metric, dmroute_origin_static);
    if (route == NULL)
    {
        Dmod_Printf("%s: failed to add route to '%s' via '%s'\n", prog, argv[1], iface_name);
        return 1;
    }

    return 0;
}

/* dmod's minimal module runtime has no libc memcmp() (see
 * dmod/src/module/string.c's replacement set, which stops at memcpy/
 * memmove/memset) - a small manual comparison stands in for it, same
 * pattern lib/dmnetif/tests/dmnetif_test.c uses. */
static bool ipv4_bytes_equal(const uint8_t* a, const uint8_t* b)
{
    for (int i = 0; i < DMROUTE_IPV4_ADDR_LEN; i++)
    {
        if (a[i] != b[i])
            return false;
    }
    return true;
}

/* Finds the route exactly matching destination/netmask/iface_name, the
 * same identity `ip route del` matches on in real iproute2 (a route is
 * identified by its destination network, not by a single address within
 * it - `dmroute_lookup()` would find the best match for an address, which
 * is not the same thing). */
typedef struct
{
    const dmroute_addr_t* destination;
    const dmroute_addr_t* netmask;
    const char*               iface_name;
    dmroute_route_t           found;
} find_exact_ctx_t;

static bool find_exact_visitor(dmroute_route_t route, void* user_data)
{
    find_exact_ctx_t* ctx = (find_exact_ctx_t*)user_data;

    dmroute_addr_t destination = { 0 };
    dmroute_addr_t netmask = { 0 };
    dmroute_get_destination(route, &destination);
    dmroute_get_netmask(route, &netmask);

    const char* iface_name = dmroute_get_iface_name(route);
    bool iface_matches = (iface_name != NULL) && (strcmp(iface_name, ctx->iface_name) == 0);
    bool family_matches = destination.family == dmroute_family_v4;
    bool addr_matches = family_matches && ipv4_bytes_equal(destination.addr.v4, ctx->destination->addr.v4);
    bool mask_matches = family_matches && ipv4_bytes_equal(netmask.addr.v4, ctx->netmask->addr.v4);

    if (iface_matches && addr_matches && mask_matches)
    {
        ctx->found = route;
        return false;
    }
    return true;
}

static int cmd_route_del(const char* prog, int argc, char* argv[])
{
    /* argv[0] == "del", argv[1] == "<dest>[/<prefixlen>]" */
    if (argc < 4 || strcmp(argv[2], "dev") != 0)
    {
        print_usage(prog);
        return 1;
    }

    dmroute_addr_t destination = { 0 };
    dmroute_addr_t netmask = { 0 };
    if (!parse_destination(argv[1], &destination, &netmask))
    {
        Dmod_Printf("%s: invalid destination '%s'\n", prog, argv[1]);
        return 1;
    }

    find_exact_ctx_t ctx = { .destination = &destination, .netmask = &netmask, .iface_name = argv[3], .found = NULL };
    dmroute_for_each(find_exact_visitor, &ctx);

    if (ctx.found == NULL)
    {
        Dmod_Printf("%s: no route to '%s' dev '%s'\n", prog, argv[1], argv[3]);
        return 1;
    }

    dmroute_remove(ctx.found);
    return 0;
}

int main(int argc, char *argv[])
{
    const char* prog = (argc > 0 && argv[0] != NULL) ? argv[0] : "ip";

    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0))
    {
        print_usage(prog);
        return 0;
    }

    if (argc < 2 || strcmp(argv[1], "route") != 0)
    {
        print_usage(prog);
        return 1;
    }

    if (argc == 2 || strcmp(argv[2], "show") == 0)
    {
        if (argc > 3)
        {
            return cmd_show_one(prog, argv[3]);
        }

        dmroute_for_each(print_route_visitor, NULL);
        return 0;
    }

    if (strcmp(argv[2], "get") == 0 && argc == 4)
    {
        return cmd_show_one(prog, argv[3]);
    }

    if (strcmp(argv[2], "add") == 0)
    {
        return cmd_route_add(prog, argc - 2, &argv[2]);
    }

    if (strcmp(argv[2], "del") == 0)
    {
        return cmd_route_del(prog, argc - 2, &argv[2]);
    }

    print_usage(prog);
    return 1;
}
