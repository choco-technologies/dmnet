/**
 * @file ifconfig.c
 * @brief ifconfig - create/list/inspect/control dmnetif-registered network interfaces
 *
 * A thin CLI over dmnetif's API - never touches devfs paths or dmdrvi
 * directly, only interface names (see dmnetif.h / lib/dmnetif/docs/dmnetif.md).
 * Output format follows net-tools' `ifconfig` (Linux) reasonably closely,
 * but only shows fields dmnetif actually tracks - no dropped/overruns/
 * collisions, none of which dmnetif has any concept of.
 */
#include "dmod.h"
#include "dmnetif.h"
#include "dmip.h"
#include <string.h>

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parses "AA:BB:CC:DD:EE:FF" into a dmnetif_mac_addr_t. Returns true on success. */
static bool parse_mac_address(const char* s, dmnetif_mac_addr_t* mac)
{
    if (s == NULL)
        return false;

    for (int i = 0; i < DMNETIF_MAC_ADDR_LEN; i++)
    {
        int hi = hex_nibble(s[0]);
        int lo = (hi >= 0) ? hex_nibble(s[1]) : -1;
        if (hi < 0 || lo < 0)
            return false;

        mac->addr[i] = (uint8_t)((hi << 4) | lo);
        s += 2;

        if (i < DMNETIF_MAC_ADDR_LEN - 1)
        {
            if (*s != ':')
                return false;
            s++;
        }
    }

    return *s == '\0';
}

/* Parses a plain decimal uint16_t (dmod's minimal module runtime has no
 * strtol()/atoi() - see dmod/src/module/string.c's replacement set). */
static bool parse_uint16(const char* s, uint16_t* out)
{
    if (s == NULL || *s == '\0')
        return false;

    uint32_t value = 0;
    for (const char* p = s; *p != '\0'; p++)
    {
        if (*p < '0' || *p > '9')
            return false;

        value = value * 10 + (uint32_t)(*p - '0');
        if (value > UINT16_MAX)
            return false;
    }

    *out = (uint16_t)value;
    return true;
}

/* Parses "A.B.C.D" (each octet 0-255) into an IPv4 dmip_addr_t. */
static bool parse_ipv4_address(const char* s, dmip_addr_t* ip)
{
    if (s == NULL)
        return false;

    ip->family = dmip_family_v4;

    for (int i = 0; i < DMIP_IPV4_ADDR_LEN; i++)
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

        if (i < DMIP_IPV4_ADDR_LEN - 1)
        {
            if (*s != '.')
                return false;
            s++;
        }
    }

    return *s == '\0';
}

static void print_mac_address(const dmnetif_mac_addr_t* mac)
{
    Dmod_Printf("%02x:%02x:%02x:%02x:%02x:%02x",
                mac->addr[0], mac->addr[1], mac->addr[2],
                mac->addr[3], mac->addr[4], mac->addr[5]);
}

static void print_ipv4_address(const dmip_addr_t* ip)
{
    Dmod_Printf("%u.%u.%u.%u", ip->addr.v4[0], ip->addr.v4[1], ip->addr.v4[2], ip->addr.v4[3]);
}

/* IPv6 printed as 8 plain hex groups (no "::" run-length compression) -
 * more verbose than canonical form, but unambiguous and simple. */
static void print_ipv6_address(const dmip_addr_t* ip)
{
    for (int i = 0; i < DMIP_IPV6_ADDR_LEN; i += 2)
    {
        Dmod_Printf("%s%02x%02x", (i > 0) ? ":" : "", ip->addr.v6[i], ip->addr.v6[i + 1]);
    }
}

/* netmask/broadcast are only ever shown alongside an IPv4 address - real
 * ifconfig has no netmask/broadcast concept for IPv6 (that's what
 * prefixlen is for, which dmnetif doesn't track). */
static void print_ip_address(dmnetif_iface_t iface, const dmip_addr_t* ip)
{
    if (ip->family == dmip_family_v4)
    {
        Dmod_Printf("        inet ");
        print_ipv4_address(ip);

        dmip_addr_t netmask;
        if (dmnetif_get_netmask(iface, &netmask) == 0 && netmask.family == dmip_family_v4)
        {
            Dmod_Printf("  netmask ");
            print_ipv4_address(&netmask);
        }

        dmip_addr_t broadcast;
        if (dmnetif_get_broadcast(iface, &broadcast) == 0 && broadcast.family == dmip_family_v4)
        {
            Dmod_Printf("  broadcast ");
            print_ipv4_address(&broadcast);
        }

        Dmod_Printf("\n");
    }
    else if (ip->family == dmip_family_v6)
    {
        Dmod_Printf("        inet6 ");
        print_ipv6_address(ip);
        Dmod_Printf("\n");
    }
}

static void print_interface(dmnetif_iface_t iface)
{
    const char* name = dmnetif_get_name(iface);
    bool up = dmnetif_is_up(iface);
    bool running = dmnetif_get_link_status(iface) == dmnetif_link_up;

    uint16_t mtu = 0;
    dmnetif_get_mtu(iface, &mtu);

    Dmod_Printf("%s: flags=<%s%s>  mtu %u\n",
                name != NULL ? name : "?",
                up ? "UP" : "DOWN",
                running ? ",RUNNING" : "",
                mtu);

    dmip_addr_t ip;
    if (dmnetif_get_ip_address(iface, &ip) == 0 && ip.family != dmip_family_none)
    {
        print_ip_address(iface, &ip);
    }

    dmnetif_mac_addr_t mac;
    Dmod_Printf("        ether ");
    if (dmnetif_get_mac_address(iface, &mac) == 0)
    {
        print_mac_address(&mac);
    }
    else
    {
        Dmod_Printf("(unknown)");
    }
    Dmod_Printf("  (Ethernet)\n");

    dmnetif_stats_t stats = { 0 };
    dmnetif_get_stats(iface, &stats);
    Dmod_Printf("        RX packets %u  bytes %u\n", stats.rx_packets, stats.rx_bytes);
    Dmod_Printf("        TX packets %u  bytes %u  errors %u\n", stats.tx_packets, stats.tx_bytes, stats.tx_errors);
}

static bool print_interface_visitor(dmnetif_iface_t iface, void* user_data)
{
    (void)user_data;
    print_interface(iface);
    Dmod_Printf("\n");
    return true;
}

static void print_usage(const char* prog)
{
    Dmod_Printf("Usage:\n");
    Dmod_Printf("  %s                              List all interfaces\n", prog);
    Dmod_Printf("  %s --help | -h                  Show this help\n", prog);
    Dmod_Printf("  %s <iface>                      Show one interface\n", prog);
    Dmod_Printf("  %s <name> create <device_path>  Register a new interface backed by a devfs node\n", prog);
    Dmod_Printf("  %s <iface> up                   Bring an interface up\n", prog);
    Dmod_Printf("  %s <iface> down                 Bring an interface down\n", prog);
    Dmod_Printf("  %s <iface> mtu <bytes>          Set the interface's MTU\n", prog);
    Dmod_Printf("  %s <iface> hw ether <mac>       Set the interface's MAC address\n", prog);
    Dmod_Printf("  %s <iface> broadcast <addr>     Set the interface's IPv4 broadcast address\n", prog);
}

int main(int argc, char *argv[])
{
    const char* prog = (argc > 0 && argv[0] != NULL) ? argv[0] : "ifconfig";

    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0))
    {
        print_usage(prog);
        return 0;
    }

    if (argc <= 1)
    {
        dmnetif_for_each(print_interface_visitor, NULL);
        return 0;
    }

    const char* name = argv[1];

    /* "create" registers a brand new interface, so it must be handled
     * before the dmnetif_find_by_name() lookup below - the interface isn't
     * expected to exist yet. */
    if (argc == 4 && strcmp(argv[2], "create") == 0)
    {
        const char* device_path = argv[3];
        dmnetif_iface_t new_iface = dmnetif_register(name, device_path);
        if (new_iface == NULL)
        {
            Dmod_Printf("%s: failed to create interface '%s' from '%s'\n", prog, name, device_path);
            return 1;
        }
        return 0;
    }

    dmnetif_iface_t iface = dmnetif_find_by_name(name);
    if (iface == NULL)
    {
        Dmod_Printf("%s: interface '%s' not found\n", prog, name);
        return 1;
    }

    if (argc == 2)
    {
        print_interface(iface);
        return 0;
    }

    const char* command = argv[2];

    if (strcmp(command, "up") == 0 && argc == 3)
    {
        int ret = dmnetif_up(iface);
        if (ret != 0)
        {
            Dmod_Printf("%s: failed to bring '%s' up (error %d)\n", prog, name, ret);
            return 1;
        }
        return 0;
    }

    if (strcmp(command, "down") == 0 && argc == 3)
    {
        int ret = dmnetif_down(iface);
        if (ret != 0)
        {
            Dmod_Printf("%s: failed to bring '%s' down (error %d)\n", prog, name, ret);
            return 1;
        }
        return 0;
    }

    if (strcmp(command, "mtu") == 0 && argc == 4)
    {
        uint16_t mtu = 0;
        if (!parse_uint16(argv[3], &mtu) || mtu == 0)
        {
            Dmod_Printf("%s: invalid MTU '%s'\n", prog, argv[3]);
            return 1;
        }

        int ret = dmnetif_set_mtu(iface, mtu);
        if (ret != 0)
        {
            Dmod_Printf("%s: failed to set MTU on '%s' (error %d)\n", prog, name, ret);
            return 1;
        }
        return 0;
    }

    if (strcmp(command, "hw") == 0 && argc == 5 && strcmp(argv[3], "ether") == 0)
    {
        dmnetif_mac_addr_t mac;
        if (!parse_mac_address(argv[4], &mac))
        {
            Dmod_Printf("%s: invalid MAC address '%s'\n", prog, argv[4]);
            return 1;
        }

        int ret = dmnetif_set_mac_address(iface, &mac);
        if (ret != 0)
        {
            Dmod_Printf("%s: failed to set MAC address on '%s' (error %d)\n", prog, name, ret);
            return 1;
        }
        return 0;
    }

    if (strcmp(command, "broadcast") == 0 && argc == 4)
    {
        dmip_addr_t broadcast;
        if (!parse_ipv4_address(argv[3], &broadcast))
        {
            Dmod_Printf("%s: invalid broadcast address '%s'\n", prog, argv[3]);
            return 1;
        }

        int ret = dmnetif_set_broadcast(iface, &broadcast);
        if (ret != 0)
        {
            Dmod_Printf("%s: failed to set broadcast address on '%s' (error %d)\n", prog, name, ret);
            return 1;
        }
        return 0;
    }

    print_usage(prog);
    return 1;
}
