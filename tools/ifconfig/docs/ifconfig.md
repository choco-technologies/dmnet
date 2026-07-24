# ifconfig

## Overview

`ifconfig` is a CLI over [dmnetif](../../../lib/dmnetif)'s API - it lists
and controls whatever network interfaces are currently registered with
dmnetif, by name. It never touches devfs paths, `dmdrvi` ioctls, or any
driver directly; everything it does goes through `dmnetif_find_by_name()`
and the handle that returns.

## Usage

```
ifconfig                              List all interfaces
ifconfig --help | -h                  Show this help
ifconfig <iface>                      Show one interface
ifconfig <name> create <device_path>  Register a new interface backed by a devfs node
ifconfig <iface> up                   Bring an interface up
ifconfig <iface> down                 Bring an interface down
ifconfig <iface> mtu <bytes>          Set the interface's MTU
ifconfig <iface> hw ether <mac>       Set the interface's MAC address
ifconfig <iface> broadcast <addr>     Set the interface's IPv4 broadcast address
```

Example output (follows net-tools' Linux `ifconfig` format, minus any
field dmnetif has no concept of - no dropped/overruns/collisions):

```
$ ifconfig
eth0: flags=<UP,RUNNING>  mtu 1500
        inet 192.168.1.42  netmask 255.255.255.0  broadcast 192.168.1.255
        ether 02:00:00:00:00:01  (Ethernet)
        RX packets 12  bytes 3456
        TX packets 8  bytes 987  errors 0

```

The `inet`/`inet6` line is only printed once an IP address has actually
been assigned to the interface (via `dmnetif_set_ip_address()`, e.g. by a
DHCP client or static config in `networkd`) - `ifconfig` itself has no
command to assign the address or netmask, only to display them once
something else has. The broadcast address is the one exception: it's both
displayed and settable directly (`ifconfig <iface> broadcast <addr>`).
There is deliberately no `RX errors` line: `dmnetif_receive()` returning 0
means either "nothing pending" or a genuine read failure, indistinguishable
at that layer, so a fabricated RX error counter would be misleading. `TX
errors` is real - the driver rejecting a frame from an up interface is
unambiguous.

`ifconfig <name> create <device_path>` registers a brand new interface via
`dmnetif_register()` - the only command that doesn't require `<name>` to
already exist. See `lib/dmnetif/docs/dmnetif.md`'s "Registration flow" for
how this compares to a driver registering its own interface.

## Exit codes

- `0` - success (including "no interfaces registered" for the no-args form,
  and `--help`/`-h`)
- `1` - failure: unknown interface name (except for `create`, where it's
  expected not to exist yet), bad/unsupported command syntax, an invalid
  MAC/MTU/broadcast value, a device path `create` couldn't open, or the
  underlying driver rejecting the requested operation (e.g. `up`/`down`/
  `mtu`/`hw ether` return whatever `dmnetif_up()`/`_down()`/`_set_mtu()`/
  `_set_mac_address()` report)

## Testing

`tests/ifconfig_test.c` drives this module through
`Dmod_RunModule("ifconfig", argc, argv)` (loads it, runs `main()`, unloads
it again - see `Dmod_RunModule` in `dmod.h`) rather than depending on
`ifconfig` as a linked module: an Application-type module can't be loaded
as another module's dependency (only a Library-type module can), and
`Dmod_RunModule` sidesteps that entirely by loading/running/unloading it on
demand instead of holding it resident for the test's lifetime.

## Dependencies

- `dmnetif` - the network interface manager this tool lists/controls (see
  `lib/dmnetif/docs/dmnetif.md`)
