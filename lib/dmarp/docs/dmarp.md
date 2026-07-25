# DMARP - DMOD ARP Resolver

## Overview

DMARP resolves an IPv4 address to a MAC address on a directly-connected
link - the piece that lets anything above the interface layer turn "send
to 192.168.1.1" into an actual destination MAC for the Ethernet header,
without hand-rolling ARP itself. It's a thin layer over
[dmnetif](../../dmnetif): a request/reply exchange sent and received
through `dmnetif_send()`/`dmnetif_receive()` on a specific interface, plus
a cache so repeated resolutions of the same destination don't re-send a
request every time.

```
┌──────────────────────────────────────────────┐
│         TCP/IP stack (networkd) / ...         │
├──────────────────────────────────────────────┤
│                  DMARP                        │
│   resolve (cache + request/reply exchange),   │
│   cache lookup/insert/remove/count            │
├──────────────────────────────────────────────┤
│                  DMNETIF                      │
│   named interfaces, send/receive one frame    │
├──────────────────────────────────────────────┤
│                  DMROUTE                      │
│   shared dmroute_addr_t type                  │
└──────────────────────────────────────────────┘
```

IPv6 is out of scope: IPv6 neighbor discovery uses NDP over ICMPv6, an
entirely different protocol, not ARP - every dmarp function only ever
deals with `dmroute_family_v4` addresses.

## Resolution

`dmarp_resolve()` checks the cache first (see below); on a miss, it:

1. Reads the interface's own MAC address (`dmnetif_get_mac_address()`) and
   IP address (`dmnetif_get_ip_address()` - a missing address is fine,
   see "Frame format" below) to build the request.
2. Sends one ARP request frame (`dmnetif_send()`) - broadcast destination,
   opcode 1 ("who has").
3. Polls `dmnetif_receive()` (every 10ms) until either a frame matching
   the request arrives (ARP, opcode 2 "is at", sender protocol address ==
   the address being resolved) or `timeout_ms` elapses.
4. Caches a successful reply (`dmarp_cache_insert()`) before returning.

Only one request is sent per `dmarp_resolve()` call - there is no
built-in retry. A caller that wants retry behavior can simply call
`dmarp_resolve()` again after a timeout; the second call is itself another
independent request/wait cycle (the first one having found nothing to
cache).

Every other frame that arrives on the interface while waiting - including
other hosts' unrelated ARP traffic - is silently ignored; only a reply
whose sender protocol address matches what was asked about is treated as
a match.

## Cache

Keyed by **(interface name, IP address)** - a name, not a `dmnetif_iface_t`
handle, the same reasoning [dmroute](../../dmroute) uses for its own
routes: a stored handle could go stale across interface churn (unregister/
re-register), a name can't. This does mean a cache entry outlives the
specific interface instance it was learned through - a re-registered
interface with the same name inherits whatever was cached for that name
before. dmarp has no dependency on dmnetif's registry to validate a name
against, the same limitation dmroute documents for its own routes.

Entries expire after `DMARP_CACHE_TTL_MS` (60 seconds) - applied
uniformly, whether an entry came from a real reply or a manual
`dmarp_cache_insert()` call. There is no separate "static/permanent"
entry concept; a manually-seeded mapping (e.g. a hardcoded gateway MAC)
needs to be re-inserted (or re-resolved) periodically to stay valid, same
as anything else in the cache. `dmarp_resolve()`/`dmarp_cache_lookup()`
both treat an expired entry as a miss.

`dmarp_cache_remove()` is for invalidating a specific mapping by hand
(e.g. after detecting an address conflict) rather than waiting out the
TTL. There is no bulk-clear - remove entries individually, or let them
expire.

## Frame format

Built/parsed as a raw 42-byte buffer (14-byte Ethernet II header + 28-byte
ARP payload for IPv4-over-Ethernet) rather than a packed C struct - see
`src/dmarp.c`'s file comment for the exact byte layout. dmod's minimal
module runtime gives no struct-packing guarantee, so this is the same
"index into a `uint8_t` buffer by hand" approach `tools/ip/src/ip.c` and
`tools/ifconfig/src/ifconfig.c` already use for address parsing.

If the resolving interface has no IP address of its own yet
(`dmnetif_get_ip_address()` returns `dmroute_family_none`), the request's
sender protocol address is written as all-zero rather than failing - a
real host can legitimately ARP before it has an address of its own (e.g.
during DHCP), so this is not treated as an error.

## Dependencies

- `dmroute` - the shared `dmroute_addr_t` type every address field uses
- `dmnetif` - `dmnetif_send()`/`_receive()` to exchange frames,
  `dmnetif_get_mac_address()`/`_get_ip_address()` to build a request,
  `dmnetif_get_name()` to key cache entries.
- `dmlist` - backs the cache
- `dmosi` - mutex guarding the cache, plus `dmosi_get_tick_count()`/
  `_thread_sleep()` for `dmarp_resolve()`'s reply poll loop
