# ifconfig

## Overview

`ifconfig` is a CLI over [dmnetif](../../../lib/dmnetif)'s API - it lists
and controls whatever network interfaces are currently registered with
dmnetif, by name. It never touches devfs paths, `dmdrvi` ioctls, or any
driver directly; everything it does goes through `dmnetif_find_by_name()`
and the handle that returns.

## Usage

```
ifconfig                          List all interfaces
ifconfig <iface>                  Show one interface
ifconfig <iface> up               Bring an interface up
ifconfig <iface> down             Bring an interface down
ifconfig <iface> hw ether <mac>   Set the interface's MAC address
```

Example output:

```
$ ifconfig
eth0       Link encap:Ethernet  HWaddr 02:00:00:00:00:01
          inet addr:192.168.1.42
          UP  LINK-UP

```

The `inet`/`inet6` line is only printed once an IP address has actually
been assigned to the interface (via `dmnetif_set_ip_address()`, e.g. by a
DHCP client or static config in `networkd`) - `ifconfig` itself has no
command to assign one, only to display it.

## Exit codes

- `0` - success (including "no interfaces registered" for the no-args form)
- `1` - failure: unknown interface name, bad/unsupported command syntax, an
  invalid MAC address string, or the underlying driver rejecting the
  requested operation (e.g. `up`/`down`/`hw ether` return whatever
  `dmnetif_up()`/`_down()`/`_set_mac_address()` report)

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
