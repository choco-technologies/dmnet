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
│   MAC address, send/receive one frame,        │
│   name <-> handle lookup                      │
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

## Frame I/O

`dmnetif_send()`/`dmnetif_receive()` move one whole frame per call,
mirroring the underlying `dmdrvi` driver's `write()`/`read()` contract.
`dmnetif_receive()` is non-blocking, returning 0 immediately if nothing is
pending - a consumer that needs to wait (e.g. a network stack's RX task)
polls on its own schedule.

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
