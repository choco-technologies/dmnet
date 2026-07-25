# DMIP - DMOD IP Protocol

## Overview

DMIP is the IP layer: building and parsing IPv4/IPv6 headers, the IPv4
header checksum, TTL (IPv4) / Hop Limit (IPv6) handling, identification
generation, and fragmentation/reassembly for both families. It also holds
`dmip_addr_t`, the address type shared by every module that speaks IP -
dmnetif tracks one per interface (address/netmask/broadcast), dmroute
matches destinations against one per route.

## Why a whole module for this

dmip started out as nothing but `dmip_addr_t`, pulled out of dmnetif so
dmroute could use the exact same address type without either module
depending on the other for it (dmnetif already depends on dmroute -
`dmnetif_set_ip_address()` calls `dmroute_add()`/`_remove()` directly to
keep an interface's connected route in sync - see
`lib/dmnetif/src/dmnetif.c` - so dmroute depending back on dmnetif for a
type would have made it a build cycle).

That same reasoning extends naturally to the rest of the IP layer: header
build/parse, checksums, TTL/hop-limit, identification and fragmentation
are all things *any* module that sends or receives IP packets needs
(dmroute forwarding a packet, a future TCP/UDP module, a future ICMP
module), and none of them are specific to a particular interface or
routing decision. Keeping them here means every consumer gets exactly one
implementation to depend on, instead of each reimplementing (and quietly
diverging on) header layout, checksum math, or fragment offset units.

## No options, no extension headers

`dmip_v4_build_header()` always emits a 20-byte header with IHL=5 (no
options) - real-world IPv4 options are rare enough on ordinary traffic
that supporting them in the *builder* would be speculative complexity for
a use case nothing in this tree has. `dmip_v4_parse_header()` still
tolerates a larger IHL on a *received* header (options are skipped, not
interpreted, via the `header_len` it reports).

Symmetrically, `dmip_v6_build_header()`/`dmip_v6_parse_header()` only
handle the 40-byte IPv6 fixed header. The one IPv6 extension header dmip
does understand is the Fragment header (RFC 8200 4.5) - entirely owned by
`dmip_v6_fragment()`/`dmip_v6_reassemble()`, since fragmentation is the
one case in this tree that actually needs it. Any other extension header
(Hop-by-Hop Options, Routing, ...) is out of scope until something
upstream of dmip actually needs one.

## Fragmentation is stateless, reassembly is not

`dmip_v4_fragment()`/`dmip_v6_fragment()` are pure functions: given a
payload and an MTU, they slice it and hand each resulting packet to a
caller-supplied callback, keeping no state of their own between calls.

`dmip_v4_reassemble()`/`dmip_v6_reassemble()` are the opposite - putting
fragments back together inherently requires remembering what's arrived so
far for a given (source, destination, protocol, identification) tuple
until either the last fragment shows up or too much time passes. Both
families share one system-wide reassembly table in `src/dmip.c` (a
`dmlist` of entries, guarded by one mutex) rather than each getting its
own - the bookkeeping (chunk list, coverage check, expiry) is completely
family-agnostic; only key construction and the final packet rebuild
differ. An entry that never completes is dropped after
`DMIP_REASSEMBLY_TIMEOUT_MS` (30s), checked opportunistically on every
`_reassemble()` call rather than by a background timer - dmod's minimal
module runtime gives every module a lifecycle, not a free-running clock
of its own.

Both `_reassemble()` functions are safe to call unconditionally on every
inbound packet: a packet that was never fragmented in the first place is
recognized immediately and handed back as-is, without ever touching the
reassembly table.

## Byte buffers, not packed structs

Like `lib/dmarp/src/dmarp.c` and `tools/ip/src/ip.c`, headers are
built/parsed as raw `uint8_t` buffers indexed by hand, not packed C
structs - dmod's minimal module runtime gives no guarantee about struct
packing across targets, and a mismatched `dmip_v4_header_t` on the wire
would corrupt every packet silently.

## Dependencies

dmlist (fragment reassembly bookkeeping) and dmosi (mutexes guarding
reassembly/identification state, plus `dmosi_get_tick_count()` for
reassembly timeouts). Nothing else - dmip still depends on no other
in-tree module, keeping dmnetif and dmroute free to depend on it without
any risk of a cycle.
