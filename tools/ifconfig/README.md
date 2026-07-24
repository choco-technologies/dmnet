# ifconfig

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](../../LICENSE)

`ifconfig` DMOD application module - a CLI to list and control the network
interfaces registered with [dmnetif](../../lib/dmnetif). It never touches
devfs paths or `dmdrvi` directly, only interface names, the same boundary
`dmnetif` itself enforces.

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
been assigned to the interface (via `dmnetif_set_ip_address()` - there is
no `ifconfig <iface> inet <addr>`-style command yet, only display).

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
