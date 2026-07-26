# DMUDP - DMOD UDP

## Overview

DMUDP builds and parses UDP segments (RFC 768) - source/destination port,
length, and a pseudo-header checksum - and sends/receives them. It does
this by calling straight into [dmip](../../dmip)'s own family-agnostic
`dmip_send()`/`dmip_receive()`: dmudp itself never talks to dmroute,
dmarp, or dmnetif directly for anything - dmip already does all of that
(route lookup, ARP resolution, frame I/O, fragmentation). Building UDP as
a thin layer over an IP layer that can already put a packet on the wire is
the entire reason dmudp exists as its own module rather than a few more
functions bolted onto dmip.

```
┌──────────────────────────────────────────────┐
│                  DMUDP                        │
│   build/parse/checksum a UDP segment,         │
│   dmudp_send()/dmudp_receive()                │
├──────────────────────────────────────────────┤
│                  DMIP                         │
│   dmip_send()/dmip_receive(), dmip_checksum() │
├──────────────────────────────────────────────┤
│         DMROUTE / DMNETIF / DMARP             │
└──────────────────────────────────────────────┘
```

## One function per direction, not one per family

There is `dmudp_send()` and `dmudp_receive()` - no `dmudp_v4_send()`/
`_v6_send()`, no `dmudp_v4_receive()`/`_v6_receive()`. A caller sending a
datagram only ever has one destination address, and that address already
carries the family that would otherwise decide which of two
same-shaped functions to call - making the caller repeat that choice via
the function name too is pure redundancy, not a real API surface. This is
the same reasoning [dmip.md](../../dmip/docs/dmip.md#family-agnostic-dmip_send-dmip_receive)
gives for `dmip_send()`/`dmip_receive()` one layer down - dmudp just
carries it one step further and drops the per-family functions
altogether rather than keeping them *and* a wrapper on top.

Receiving is, if anything, an even clearer case: which family a datagram
turns out to be *is* the thing waiting is trying to discover, so it was
never something a caller could have picked by calling a specific function
in the first place - `dmudp_receive()` reports it back via `out_family`.

## Why the source address has to be known up front

A UDP checksum is computed over a pseudo-header that includes the
*source* IP address (RFC 768) - but picking a source address is normally
something the IP layer does internally, after a route lookup, right
before it actually sends. `dmudp_send()` can't wait for that: the segment
(including its checksum) has to be fully built *before* it's handed to
`dmip_send()` as a payload.

The fix is `dmip_v4_get_source_address()` - a function `dmip_v4_send()`
already uses internally when its caller leaves the source unset, exposed
publicly for exactly this reason. For an IPv4 destination, `dmudp_send()`
calls it first to learn the source address, builds the segment and its
checksum against that, then calls `dmip_send()` with an explicit
`header.header.v4.src` so the two never disagree.

## One buffer, no extra copy

Both `dmudp_send()` and `dmudp_v4_checksum_valid()`/`_v6_checksum_valid()`
build a single buffer shaped `[pseudo-header][UDP header][payload]` and
run `dmip_checksum()` over the whole thing - the pseudo-header prefix is
never transmitted, only summed. `dmudp_send()` goes one step further:
since the segment it needs to send is already sitting right after the
pseudo-header in that same buffer, it hands `dmip_send()` a pointer into
the middle of it rather than allocating and copying a second time.

## IPv4: checksum 0 means "none"; IPv6: checksum is mandatory

RFC 768 lets an IPv4 UDP sender skip the checksum entirely by writing 0 in
the checksum field. `dmudp_send()` never does this itself (it always
computes a real checksum, remapping a computed value of exactly 0 to
0xFFFF per the RFC), but `dmudp_receive()` respects it on the way in for
an IPv4 datagram - a wire checksum of 0 skips verification rather than
being treated as a mismatch.

RFC 8200 removed that allowance for IPv6: the checksum is mandatory, so
for an IPv6 datagram `dmudp_receive()` always calls
`dmudp_v6_checksum_valid()`, no special-casing for a wire value of 0
(which would essentially never happen for a real non-empty checksum sum
anyway, but there's no reason to carve out an exception for it that RFC
8200 itself doesn't allow).

## No IPv6 send yet

`dmudp_send()` returns `-ENOSYS` for an IPv6 destination: sending needs
`dmip_send()`'s IPv4 path (`dmip_v4_send()`), which resolves a destination
MAC via ARP - IPv6 needs the NDP equivalent instead, and there is no NDP
module in this tree yet (see
[dmip.md](../../dmip/docs/dmip.md#send--receive)). Receiving has no such
gap - `dmudp_receive()` handles an inbound IPv6 datagram exactly like an
IPv4 one, verifying its checksum with `dmudp_v6_checksum_valid()` instead.
Once `dmip_send()` gains an IPv6 path, `dmudp_send()`'s `dmip_family_v6`
case follows the exact shape of its existing IPv4 one - no new public
function needed.

## No socket layer

dmudp is stateless - there is no `dmudp_socket_create()`/`bind()`/`close()`,
no per-port registry, no receive queue of its own (it waits on dmip's
internal one, but has no state of its own to guard). Every call is a
one-shot send or a one-shot wait-and-parse, the same shape
`dmip_send()`/`_receive()` already have. A caller that wants a socket-like
abstraction (bind to a port, dispatch received datagrams by destination
port) can build it on top of `dmudp_receive()`'s `out_dst_port` - the same
way dmudp itself is built on top of dmip rather than dmip growing
transport-layer state.

## Byte buffers, not packed structs

Same reasoning as `dmip.c`/`dmarp.c`: segments are built/parsed as raw
`uint8_t` buffers indexed by hand, not packed C structs - dmod's minimal
module runtime gives no struct-packing guarantee.

## Dependencies

- `dmip` - `dmip_send()`/`_receive()` do the actual work, plus
  `dmip_checksum()` for the pseudo-header checksum and
  `dmip_v4_get_source_address()`/`_v4_next_identification()`
- `dmroute` - `dmip_addr_t`'s real definition (`dmroute_addr_t`)
- `dmnetif` - header-only: `dmnetif_iface_t` for `dmudp_receive()`'s
  optional `out_iface` parameter. dmudp.c never calls a `dmnetif_*`
  function directly - `dmip_receive()` already does all frame I/O
