# DMIP - DMOD IP Protocol Types

## Overview

DMIP holds type definitions shared by every module that speaks IP.
Today that's just `dmip_addr_t` - dmnetif tracks one per interface
(address/netmask/broadcast), dmroute matches destinations against one per
route - but it's the natural home for whatever else needs to be common
ground at the IP layer later (a packet header type, once something in
this tree actually builds/parses IP packets, is the obvious next
candidate).

## Why a whole module for one struct

Before dmip existed, dmnetif defined its own `dmnetif_ip_addr_t` and
dmroute would have needed either a copy of the same shape or a header
dependency on dmnetif - but dmnetif *also* needs to depend on dmroute
(`dmnetif_set_ip_address()` calls `dmroute_add()`/`_remove()` directly to
keep an interface's connected route in sync - see
`lib/dmnetif/src/dmnetif.c`), which would have made dmnetif -> dmroute ->
dmnetif a build cycle if dmroute also depended on dmnetif for the address
type.

Pulling the address type out into its own dependency-free module breaks
that cycle cleanly: dmnetif and dmroute both depend on dmip; dmip depends
on nothing; nothing depends on dmnetif for a *type*, only for its actual
interface-registry behavior. It also means the two modules' address
representations can never quietly drift apart the way two independent
copies eventually would.

## No runtime behavior

dmip has no public API functions and no state - `dmod_init()`/
`dmod_deinit()` are empty stubs, present only because every DMOD module
needs them. It exists as a real module (rather than a header vendored
into both dmnetif and dmroute) purely so there is exactly one place for
IP-layer types to live and grow from. Nothing needs to load `dmip.dmf` at
runtime to use its types - `dmip_if` (headers only) is all any consumer
links against.

## Dependencies

None.
