# ifconfig

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](../../LICENSE)

`ifconfig` DMOD application module - a CLI to list and control the network
interfaces registered with [dmnetif](../../lib/dmnetif). It never touches
devfs paths or `dmdrvi` directly, only interface names, the same boundary
`dmnetif` itself enforces.

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
been assigned to the interface (via `dmnetif_set_ip_address()` - there is
no `ifconfig <iface> inet <addr>`-style command yet, only display); the
`netmask`/`broadcast` parts of that line only appear once those are
assigned too (`dmnetif_set_netmask()`, or `ifconfig <iface> broadcast
<addr>` for the broadcast address - unlike the address and netmask, that
one is directly settable from `ifconfig`). There is no `RX errors` line
either - `dmnetif_receive()` returning 0 means either "nothing pending" or
a genuine read failure, indistinguishable at that layer, so a fabricated RX
error counter would be misleading; `TX errors` is real (the driver
rejecting a frame from an up interface is unambiguous).

`ifconfig <name> create <device_path>` is the one command that doesn't
require `<name>` to already be a registered interface - it calls
`dmnetif_register()` directly, the same call a driver would make from its
own `dmdrvi_path_ready()` (see `lib/dmnetif/docs/dmnetif.md`). Useful for
registering an interface manually - e.g. without a real driver, for
testing.

## Building

This module lives under `tools/ifconfig` inside the `dmnet` repository and
is built as part of the parent's CMake configure (the top-level
`CMakeLists.txt` calls `add_subdirectory(tools)`, whose own `CMakeLists.txt`
calls `add_subdirectory(ifconfig)`) - it is not built standalone.

```bash
mkdir -p build
cd build
cmake ..
cmake --build . --target ifconfig
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub.

## Testing

`tests/ifconfig_test.c` drives this module through `Dmod_RunModule("ifconfig",
argc, argv)` (loads it, runs `main()`, unloads it again - see `Dmod_RunModule`
in `dmod.h`) rather than depending on `ifconfig` as a linked module: an
Application-type module can't be loaded as another module's dependency
(only a Library-type module can), and `Dmod_RunModule` sidesteps that
entirely by loading/running/unloading it on demand instead of holding it
resident for the test's lifetime.

## Documentation

See the `docs/` directory:

- **[ifconfig.md](docs/ifconfig.md)** - Usage, exit codes, and how this
  module is tested

View documentation using `dmf-man ifconfig`.

## Project Structure

```
ifconfig/
├── docs/
│   ├── README.md
│   └── ifconfig.md
├── src/
│   └── ifconfig.c
├── tests/
│   ├── CMakeLists.txt
│   └── ifconfig_test.c
├── CMakeLists.txt
└── ifconfig.dmr
```

LICENSE is shared with the rest of the `dmnet` repository (`../../LICENSE`).

## Author

Patryk Kubiak

## License

MIT
