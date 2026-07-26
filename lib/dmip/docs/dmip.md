# DMIP - DMOD IP Protocol

## Overview

DMIP is the IP layer: building and parsing IPv4/IPv6 headers, the IPv4
header checksum, TTL (IPv4) / Hop Limit (IPv6) handling, identification
generation, fragmentation/reassembly for both families, and sending/
receiving actual packets on the wire. `dmip_addr_t` - the address type
every module in this tree uses - is re-exported here from
[dmroute](../../dmroute), which owns the real definition (see "Address
type" below).

## Why a whole module for this

Header build/parse, checksums, TTL/hop-limit, identification and
fragmentation are all things *any* module that sends or receives IP
packets needs (dmroute forwarding a packet, dmudp, a future ICMP module),
and none of them are specific to a particular interface or routing
decision. Keeping them here means every consumer gets exactly one
implementation to depend on, instead of each reimplementing (and quietly
diverging on) header layout, checksum math, or fragment offset units.

## Address type: re-exported from dmroute, not defined here

dmip used to own `dmip_addr_t` outright, back when it had no dependencies
of its own. Once dmip needed to actually *send* a packet - which means
asking dmroute which interface/gateway to use - that stopped working:
dmroute would have needed dmip for the address type, and dmip would have
needed dmroute to send, a cycle.

The fix was to move the real definition to dmroute (`dmroute_addr_t`/
`dmroute_family_t` in `dmroute.h`) - the base of this tree's module graph,
with no dependencies of its own - and have `dmip.h` re-typedef it back:

```c
typedef dmroute_family_t dmip_family_t;
#define dmip_family_none dmroute_family_none
#define dmip_family_v4   dmroute_family_v4
#define dmip_family_v6   dmroute_family_v6
typedef dmroute_addr_t dmip_addr_t;
```

Every existing dmip.h consumer (starting with dmip.c itself) keeps working
under the `dmip_*` names completely unchanged - it's the same type either
way, just owned by dmroute. See `lib/dmroute/docs/dmroute.md` for the full
rationale from dmroute's side.

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

## Send / receive

dmip no longer touches routing, ARP, or raw frame I/O itself - all of that
moved to [dmnetbridge](../../dmnetbridge), the layer between dmip's
IP-address-level view of the world and the actual network interfaces.

`dmip_v4_send()` fills in `header->src` via `dmip_v4_get_source_address()`
if left unset, reads the egress interface's MTU (best-effort, via
`dmnetbridge_get_mtu()`), fragments the packet (the existing
`dmip_v4_fragment()`), and hands each fragment to `dmnetbridge_send()`
individually - which does the route lookup, ARP resolution, and Ethernet
framing/transmit that used to live here. A multi-fragment packet therefore
re-resolves the route/MAC once per fragment rather than once for the whole
packet - a deliberate tradeoff for dmip having zero knowledge of
interfaces/routing/ARP (see `send_v4_fragment()` in `src/dmip.c`).
`dmip_v4_get_source_address()` itself is now a thin call to
`dmnetbridge_get_source_address()`.

Receiving is push-based, not polled: dmip implements dmnetbridge's
`packet_received` DIF (`dmip_dmnetbridge_packet_received()` in
`src/dmip.c`, following the naming dmod's DIF macros generate) - called by
whichever thread is pumping *any* interface
(`dmnetbridge_handle_netif_rx()`, run by the `networkd` service), for
every frame it reads. The implementation checks the Ethertype, strips the
14-byte L2 header, and feeds the rest through `dmip_v4_reassemble()`/
`_v6_reassemble()` exactly as before - a completed packet is pushed onto
an internal queue (`g_rx_queue`, guarded by a mutex and a semaphore,
`g_rx_signal` - same shape as `dmdevfs.c`'s own hotplug-event queue, not a
fixed-size ring) instead of being returned directly, since the caller of
`dmip_v4_receive()`/`_v6_receive()`/`_receive()` is essentially never the
same thread that received the frame. Those three functions wait on that
queue up to a caller-supplied `timeout_ms`, scanning for an entry matching
the family they want (v4-only / v6-only / either) - see "Receive queue" in
`src/dmip.c` for the full picture. None of them take an interface
parameter anymore; each has an optional `out_iface` instead, filled in
with whichever interface the returned packet actually arrived on.

There is no `dmip_v6_send()`: resolving a destination MAC for IPv6 uses
NDP (RFC 4861), not ARP, and there is no NDP module in this tree yet -
the same boundary `dmarp.h` documents for itself regarding IPv6. Once an
NDP module exists, `dmip_v6_send()` can be added following the exact shape
of `dmip_v4_send()` (via `dmnetbridge_send()`, same as IPv4).

## Family-agnostic `dmip_send()`/`dmip_receive()`

A caller already has to say which family it means once - by populating
either `dmip_v4_header_t` or `dmip_v6_header_t` (they don't share a field
layout, so there's no way around picking one) - so making it *also* pick
which function to call (`dmip_v4_send()` vs. a `dmip_v6_send()`) on top of
that is redundant. `dmip_send()` takes a `dmip_header_t` (the same two
header structs behind a `family` tag, needed because the structs don't
share a common initial sequence so the tag can't be inferred safely) and
dispatches to `dmip_v4_send()` itself, or `-ENOSYS` for `dmip_family_v6`
until `dmip_v6_send()` exists.

`dmip_receive()` is not just a convenience on the receive side - it fixes
a real gap: `dmip_v4_receive()` and `dmip_v6_receive()` each only ever
match their own family's queue entries, so polling both in a loop would
starve whichever one is called second behind the first if packets of both
families keep arriving. `dmip_receive()` waits on the same queue for
*either* family and returns whichever was queued first, reporting which
family it got via an output parameter.

## Byte buffers, not packed structs

Like `lib/dmarp/src/dmarp.c` and `tools/ip/src/ip.c`, headers are
built/parsed as raw `uint8_t` buffers indexed by hand, not packed C
structs - dmod's minimal module runtime gives no guarantee about struct
packing across targets, and a mismatched `dmip_v4_header_t` on the wire
would corrupt every packet silently.

## Dependencies

- `dmroute` - header-only: the address type (re-exported as `dmip_addr_t`).
  dmip.c itself never calls a `dmroute_*` function anymore - that moved to
  dmnetbridge
- `dmnetif` - header-only: `dmnetif_iface_t` for the optional `out_iface`
  parameter on the receive functions. dmip.c never calls a `dmnetif_*`
  function directly anymore either
- `dmnetbridge` - `dmnetbridge_send()`/`_get_source_address()`/`_get_mtu()`
  for `dmip_v4_send()`, and the `packet_received` DIF dmip implements for
  receiving
- `dmlist` - fragment reassembly bookkeeping, and the receive queue
- `dmosi` - mutexes/semaphore guarding reassembly/identification/receive-
  queue state, plus `dmosi_get_tick_count()` for reassembly timeouts
