#ifndef DMIP_H
#define DMIP_H

#include <stdint.h>
#include "dmip_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file dmip.h
 * @brief DMOD IP protocol types - shared IP-layer type definitions
 *
 * dmip holds type definitions shared by every module that speaks IP -
 * today that's just the address type (dmnetif tracks one per interface,
 * dmroute matches against one per route), but it's the natural home for
 * whatever else needs to be common ground at the IP layer later (e.g. a
 * packet header type once something in this tree actually builds/parses
 * IP packets).
 *
 * dmip has no functions and no runtime state at all - it's a pure header
 * dependency (dmip_if), never actually loaded as a module by anything
 * that only needs its types. It exists as a real DMOD module (rather than
 * just a shared header vendored into dmnetif/dmroute) so it has exactly
 * one place to grow from, instead of dmnetif and dmroute's copies quietly
 * drifting apart.
 *
 * dmnetif and dmroute both depend on dmip; dmip depends on nothing. This
 * is what keeps dmnetif -> dmroute (dmnetif calls dmroute_add()/_remove()
 * directly, see lib/dmnetif/src/dmnetif.c) from becoming a build cycle -
 * neither of them needs the other's headers for the address type anymore.
 */

/**
 * @brief IP address family - which member of dmip_addr_t's addr union is valid
 */
typedef enum
{
    dmip_family_none = 0,    /**< No address assigned */
    dmip_family_v4   = 4,    /**< addr.v4 is valid */
    dmip_family_v6   = 6,    /**< addr.v6 is valid */
} dmip_family_t;

/**
 * @brief Length in bytes of an IPv4 address
 */
#define DMIP_IPV4_ADDR_LEN 4

/**
 * @brief Length in bytes of an IPv6 address
 */
#define DMIP_IPV6_ADDR_LEN 16

/**
 * @brief A single IP address, either IPv4 or IPv6
 *
 * One type covering both families (discriminated by `family`) rather than
 * separate dmip_ipv4_addr_t/dmip_ipv6_addr_t types and parallel _v4/_v6
 * function pairs everywhere an address is used - callers branch on
 * `family` once, not per function.
 */
typedef struct
{
    dmip_family_t family;
    union
    {
        uint8_t v4[DMIP_IPV4_ADDR_LEN];
        uint8_t v6[DMIP_IPV6_ADDR_LEN];
    } addr;
} dmip_addr_t;

#ifdef __cplusplus
}
#endif

#endif // DMIP_H
