# ip

## Overview

`ip` is a CLI over [dmroute](../../../lib/dmroute)'s API - it inspects and
controls the IP routing table, following `iproute2`'s `ip route` subset
closely enough to be familiar. Like `ifconfig` does for `dmnetif`, it never
touches dmroute's internals directly, only its public add/remove/lookup/
for_each functions.

Only `ip route ...` is implemented - there is no `ip addr`/`ip link`
(that's what [ifconfig](../../ifconfig) is for) - and only IPv4: dmroute
itself is family-agnostic, but this CLI has no IPv6 text parser yet. A
route to an IPv6 destination (possible if an interface is given an IPv6
address - see dmroute's automatic registration below) still shows up in
`ip route show`, just as `(unsupported address family)` rather than a
decoded address.

## Usage

```
ip route [show]                                                         List all routes
ip route show <dest>                                                    Show the best-matching route for <dest>
ip route get <dest>                                                     Same as 'route show <dest>'
ip route add <dest>[/<prefixlen>] [via <gw>] dev <iface> [metric <n>]   Add a route
ip route del <dest>[/<prefixlen>] dev <iface>                           Remove a route
ip --help | -h                                                          Show this help
```

`<dest>` is an IPv4 address, `"A.B.C.D/N"` CIDR notation, or `"default"`
(equivalent to `0.0.0.0/0`). A bare address without `/N` is a host route
(`/32`), matching `iproute2`'s own default.

Example output:

```
$ ip route
default via 192.168.1.1 dev eth0 metric 50
192.168.1.0/24 dev eth0 metric 0 connected
10.0.0.0/8 via 192.168.1.254 dev eth0 metric 100
```

The `connected` tag marks a route dmroute added automatically because an
interface was assigned an IP address in that subnet (see below) - it's not
a Linux `proto`/`scope` field, just an honest label for what dmroute
actually tracks.

`ip route del` matches a route by its exact destination network + netmask
+ egress interface (the same identity `iproute2` uses) - not by looking up
which route a single address would take, which is what `route show
<dest>`/`route get` do instead.

## Automatic registration

You do not need to run `ip route add` for an interface's own subnet - as
soon as any code calls `dmnetif_set_ip_address()` on an interface (a DHCP
client, static config in `networkd`, or `ifconfig`'s own future
`inet`-setting command), dmroute adds a `connected` route for it
automatically. See
[dmroute/docs/dmroute.md](../../../lib/dmroute/docs/dmroute.md#automatic-registration)
for exactly how that works - `ip` itself has nothing to do with it, and
`ip route show` will report the route whether or not `ip` was ever run.

## Building

This module lives under `tools/ip` inside the `dmnet` repository and is
built as part of the parent's CMake configure (the top-level
`CMakeLists.txt` calls `add_subdirectory(tools)`, whose own
`CMakeLists.txt` calls `add_subdirectory(ip)`) - it is not built
standalone.

```bash
mkdir -p build
cd build
cmake ..
cmake --build . --target ip
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub.

## Testing

`tests/ip_test.c` drives this module through `Dmod_RunModule("ip", argc,
argv)` (loads it, runs `main()`, unloads it again - see `Dmod_RunModule` in
`dmod.h`) rather than depending on `ip` as a linked module - same reason
and same pattern as `tools/ifconfig/tests/ifconfig_test.c`: an
Application-type module can't be loaded as another module's dependency, so
`Dmod_RunModule` loads/runs/unloads it on demand instead of holding it
resident for the test's lifetime.
