# DMROUTE - DMOD IP Routing Table

## Overview

DMROUTE is a longest-prefix-match IP routing table. It answers one
question: "which interface (and gateway, if any) should a packet to this
destination go through?" - it never sends or receives a frame itself,
that's [dmnetif](../../dmnetif)'s `dmnetif_send()`/`_receive()` job.

```
┌──────────────────────────────────────────────┐
│      TCP/IP stack (networkd) / ip / netctl    │
├──────────────────────────────────────────────┤
│                  DMNETIF                      │
│   named interfaces, IP/netmask bookkeeping,   │
│   keeps its interfaces' connected routes in   │
│   sync by calling into DMROUTE directly       │
├──────────────────────────────────────────────┤
│                  DMROUTE                      │
│   add/remove routes, longest-prefix lookup,   │
│   enumeration, owns the shared dmroute_addr_t │
│   address type                                │
└──────────────────────────────────────────────┘
```

dmroute is the base of this tree's module graph - it has no dependency on
any other in-tree module, and owns `dmroute_addr_t` (the address type used
throughout: dmnetif tracks one per interface, dmarp resolves one to a MAC,
dmip builds packets addressed with one) so that nothing it needs pulls in a
dependency of its own. `dmip` depends on dmroute (to pick an egress
interface when sending a packet - see [dmip](../../dmip)), so dmroute
depending back on dmip for the address type would make the graph a cycle;
`dmip.h` re-exports the same type as `dmip_addr_t` for source
compatibility (see dmip's own docs) - it's the same type either way.

Note the direction versus dmnetif: dmroute sits *below* dmnetif here, not
on top of it - dmnetif is the one with a dependency on dmroute (see
"Automatic registration" below), and dmroute has no dependency on dmnetif
at all. A
route entry is a destination network (address + netmask), an optional
gateway, an egress interface *name* (a plain string dmroute never
resolves or validates - see "No dmnetif dependency" below), and a metric
used to break ties between two routes that match a destination equally
specifically.

## Automatic registration

Every dmnetif interface reports itself to dmroute automatically as soon as
it's assigned an IP address - nothing else (networkd, a DHCP client,
`ifconfig`) has to remember to add the connected route by hand.

This is not a callback/notification mechanism - `dmnetif_set_ip_address()`
(in `lib/dmnetif/src/dmnetif.c`) simply calls `dmroute_add()`/
`dmroute_remove()` directly, the same way any other caller would, tracking
the resulting `dmroute_route_t` handle on the interface (see
`update_connected_route()`):

- If the interface has no netmask on record yet (e.g. a DHCP client set
  the address slightly before the netmask), dmnetif falls back to an
  all-ones host mask when calling `dmroute_add()`, so the interface is at
  least reachable by its own address in the meantime.
- If the address is cleared (`dmroute_family_none`), dmnetif calls
  `dmroute_remove()` and adds nothing new.
- Each call replaces *that interface's own* previously added connected
  route (dmnetif holds the handle) - it never touches a static route to
  the same destination added via `dmroute_add()` directly (e.g. from `ip
  route add`), even if they happen to overlap.
- `dmnetif_unregister()` also removes the interface's connected route, so
  a torn-down interface never leaves a stale route behind.

dmnetif passes `dmroute_origin_connected` when it calls `dmroute_add()`
for this, purely as a label `dmroute_get_origin()`/`ip route show` can
read back - it has no effect on lookup.

## No dmnetif dependency

dmroute does not depend on dmnetif, on purpose: dmnetif already depends on
dmroute (see above), and a dependency in both directions would be a build
cycle. Practically, this means:

- `dmroute_add()`'s `iface_name` parameter is never checked against
  dmnetif's registry - dmroute has no way to. Callers that care whether an
  interface actually exists (e.g. `ip route add`) must check with
  `dmnetif_find_by_name()` themselves before calling `dmroute_add()`.
- Route addresses use `dmroute_addr_t`, defined right here rather than
  borrowed from dmnetif or any other module - dmnetif's own address fields
  use this same type, not the other way around.

## Longest-prefix match

`dmroute_lookup()` walks every route and keeps the one whose netmask has
the most bits set among those where `(destination_ip & route->netmask) ==
route->destination` - the standard "most specific match wins" rule. Ties
(same prefix length) are broken by the lower `metric`; a further tie keeps
whichever route was added first. There is no other route-selection
mechanism (no per-route "preferred" flag, no source-based routing) -
metric is the only manual override available via `dmroute_add()`.

Destinations are stored pre-masked: `dmroute_add()` always applies
`netmask` to `destination` before storing it, so passing a host address
alongside a network's netmask (`dmroute_add(&host_ip, &netmask, ...)`)
behaves identically to passing the network address itself.

## IPv4 and IPv6

A route's destination/netmask/gateway share one `dmroute_addr_t` type
discriminated by `family` rather than separate v4/v6 types - a route entry,
a lookup, and the prefix-length/masking math all branch on `family` once
rather than needing parallel code paths.

## Dependencies

- `dmlist` - backs the route table
- `dmosi` - mutex guarding the route table
