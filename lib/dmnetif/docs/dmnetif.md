# DMNETIF - DMOD Network Interface Manager

## Overview

DMNETIF is the boundary between devfs and the network. A network driver
(`dmeth`, and later others - loopback, PPP, Wi-Fi, ...) shows up in devfs as
an ordinary device file (`/dev/dmeth0`) managed through `dmdrvi`, exactly
like any other driver. DMNETIF is what turns that device file into a named
network interface (`"eth0"`) that a TCP/IP stack or CLI tool can address
without ever knowing devfs paths or dmdrvi ioctl commands exist.

```
┌──────────────────────────────────────────────┐
│  TCP/IP stack (networkd) / netctl / ifconfig  │
├──────────────────────────────────────────────┤
│                  DMNETIF                      │
│   register/unregister, up/down, link status,  │
│   MAC/IP address, MTU, send/receive one frame,│
│   packet stats, name <-> handle lookup        │
├──────────────────────────────────────────────┤
│         DMDRVI (open/read/write/ioctl)        │
├──────────────────────────────────────────────┤
│   Network driver (dmeth, ...) + DMDEVFS        │
└──────────────────────────────────────────────┘
```

## Registration flow

A driver cannot call `dmnetif_register()` from its own `dmod_init()` -
`dmdevfs` has not necessarily created the driver's device node yet at that
point (device creation happens when `dmdevfs` later drives the driver's
`dmdrvi_create()`/`dmdrvi_path_ready()` DIFs). The natural place to register
is the driver's `dmdrvi_path_ready()` implementation, which fires once the
device's absolute path is actually known:

```c
dmod_dmdrvi_dif_api_declaration(1.0, dmeth, void, _path_ready,
    ( dmdrvi_context_t context, const dmdrvi_dev_num_t* dev_num, const char* path ))
{
    dmnetif_register(context->config.if_name, path);
}
```

`dmnetif_register()` opens `path` itself (the same `Dmod_FileOpen`/`Ioctl`
SAL any application would use) and keeps it open for the interface's
lifetime - the driver never opens its own device file.

Nothing about `dmnetif_register()` requires the caller to be a driver,
though - `ifconfig <name> create <device_path>` calls it directly too, for
registering an interface manually (e.g. against a devfs node a driver
already exposed under a different name, or for testing without a real
driver at all).

## IP address

Unlike the MAC address, an interface's IP address is never pushed down to
the driver via a `dmdrvi` ioctl - it's a network-layer concept the
driver/hardware has no notion of at all. `dmnetif_get_ip_address()`/
`_set_ip_address()` are purely local bookkeeping: whatever assigns
addresses above DMNETIF (a DHCP client or static config in `networkd`)
calls `dmnetif_set_ip_address()`, and anything else that needs to know an
interface's address (e.g. `ifconfig`) reads it back with
`dmnetif_get_ip_address()`.

`dmnetif_ip_addr_t` is one type for both IPv4 and IPv6 (a `family` tag plus
a `v4[4]`/`v6[16]` union) rather than separate types and function pairs per
family - callers branch on `family` once, not per function.

`dmnetif_get_netmask()`/`_set_netmask()` and `_get_broadcast()`/
`_set_broadcast()` sit right next to the address and reuse the same
`dmnetif_ip_addr_t` type - both are shaped exactly like an address (4 or 16
raw bytes), which is exactly why an IPv4 netmask/broadcast is conventionally
written as a dotted-quad value ("255.255.255.0") in the first place.
Nothing calls `dmnetif_set_netmask()` yet - it exists so whatever assigns
the address (DHCP client or static config in `networkd`) has somewhere to
put the netmask alongside it, rather than that becoming an afterthought
bolted on later. `dmnetif_set_broadcast()` is the one exception that's
already wired up end to end: `ifconfig <iface> broadcast <addr>` calls it
directly.

## MTU

Same story as the IP address: an MTU is a concept dmnetif tracks for
consumers to read (e.g. `ifconfig`'s `mtu` line), not something pushed down
to the driver - there is no `DMDRVI_IOCTL_NET_*` command for it. A newly
registered interface starts at `DMNETIF_DEFAULT_MTU` (1500, the standard
Ethernet payload size) until `dmnetif_set_mtu()` changes it.

## Frame I/O

`dmnetif_send()`/`dmnetif_receive()` move one whole frame per call,
mirroring the underlying `dmdrvi` driver's `write()`/`read()` contract.
`dmnetif_receive()` is non-blocking, returning 0 immediately if nothing is
pending - a consumer that needs to wait (e.g. a network stack's RX task)
polls on its own schedule.

## Statistics

Every `dmnetif_send()`/`_receive()` call updates `dmnetif_stats_t` counters
on the interface (`rx_packets`/`rx_bytes`/`tx_packets`/`tx_bytes`/
`tx_errors`), readable via `dmnetif_get_stats()`. There is deliberately no
`rx_errors` counter: `dmnetif_receive()` returning 0 means either "no frame
currently pending" or a genuine read failure, and the underlying `dmdrvi`
`read()` contract does not distinguish the two - counting one as an error
would be misleading. `tx_errors` has no such ambiguity: it only increments
when the interface is up (so a frame was genuinely attempted) and the
driver's `write()` still returned 0, which unambiguously means the driver
rejected the frame.

## Escape hatch

`dmnetif_ioctl()` forwards a raw ioctl straight to the interface's
underlying device file, for driver-specific commands beyond the generic
control surface (start/stop, link status, MAC address) - e.g. `dmeth`'s
`DMETH_IOCTL_SET_PROMISCUOUS_MODE`. The caller must already know which
driver backs the interface to interpret `command`/`arg`; every other
DMNETIF function keeps that knowledge out of its callers by design, this
one intentionally does not.

## Dependencies

- `dmdrvi` - generic driver interface (open/close/read/write/ioctl,
  `DMDRVI_IOCTL_NET_*` commands) used internally to talk to the device
  a driver registered
