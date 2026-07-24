/**
 * @file ifconfig.c
 * @brief ifconfig - list/inspect/control dmnetif-registered network interfaces
 *
 * A thin CLI over dmnetif's API - never touches devfs paths or dmdrvi
 * directly, only interface names (see dmnetif.h / lib/dmnetif/docs/dmnetif.md).
 */
#include "dmod.h"
#include "dmnetif.h"
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

static void print_mac_address(const dmnetif_mac_addr_t* mac)
{
    Dmod_Printf("%02x:%02x:%02x:%02x:%02x:%02x",
                mac->addr[0], mac->addr[1], mac->addr[2],
                mac->addr[3], mac->addr[4], mac->addr[5]);
}

/* IPv6 printed as 8 plain hex groups (no "::" run-length compression) -
 * more verbose than canonical form, but unambiguous and simple. */
static void print_ip_address(const dmnetif_ip_addr_t* ip)
{
    if (ip->family == dmnetif_ip_family_v4)
    {
        Dmod_Printf("          inet addr:%u.%u.%u.%u\n",
                    ip->addr.v4[0], ip->addr.v4[1], ip->addr.v4[2], ip->addr.v4[3]);
    }
    else if (ip->family == dmnetif_ip_family_v6)
    {
        Dmod_Printf("          inet6 addr:");
        for (int i = 0; i < DMNETIF_IPV6_ADDR_LEN; i += 2)
        {
            Dmod_Printf("%s%02x%02x", (i > 0) ? ":" : "", ip->addr.v6[i], ip->addr.v6[i + 1]);
        }
        Dmod_Printf("\n");
    }
}

static void print_interface(dmnetif_iface_t iface)
{
    const char* name = dmnetif_get_name(iface);
    Dmod_Printf("%-10s Link encap:Ethernet  HWaddr ", name != NULL ? name : "?");

    dmnetif_mac_addr_t mac;
    if (dmnetif_get_mac_address(iface, &mac) == 0)
    {
        print_mac_address(&mac);
    }
    else
    {
        Dmod_Printf("(unknown)");
    }
    Dmod_Printf("\n");

    dmnetif_ip_addr_t ip;
    if (dmnetif_get_ip_address(iface, &ip) == 0 && ip.family != dmnetif_ip_family_none)
    {
        print_ip_address(&ip);
    }

    Dmod_Printf("          %s  %s\n",
                dmnetif_is_up(iface) ? "UP" : "DOWN",
                dmnetif_get_link_status(iface) == dmnetif_link_up ? "LINK-UP" : "LINK-DOWN");
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
    Dmod_Printf("  %s                          List all interfaces\n", prog);
    Dmod_Printf("  %s <iface>                  Show one interface\n", prog);
    Dmod_Printf("  %s <iface> up               Bring an interface up\n", prog);
    Dmod_Printf("  %s <iface> down             Bring an interface down\n", prog);
    Dmod_Printf("  %s <iface> hw ether <mac>   Set the interface's MAC address\n", prog);
}

int main(int argc, char *argv[])
{
    const char* prog = (argc > 0 && argv[0] != NULL) ? argv[0] : "ifconfig";

    if (argc <= 1)
    {
        dmnetif_for_each(print_interface_visitor, NULL);
        return 0;
    }

    const char* name = argv[1];
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

    print_usage(prog);
    return 1;
}
