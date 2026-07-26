# ip

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](../../LICENSE)

`ip` DMOD application module - a CLI to inspect and control the IP routing
table managed by dmroute. It never touches dmroute's
internals directly, only its public add/remove/lookup/for_each API, the
same boundary `ifconfig` enforces around `dmnetif`.

## Usage

```
ip route [show]                                                         List all routes
ip route show <dest>                                                    Show the best-matching route for <dest>
ip route get <dest>                                                     Same as 'route show <dest>'
ip route add <dest>[/<prefixlen>] [via <gw>] dev <iface> [metric <n>]   Add a route
ip route del <dest>[/<prefixlen>] dev <iface>                           Remove a route
ip --help | -h                                                          Show this help
```

`<dest>` is an IPv4 address, `"A.B.C.D/N"` CIDR notation, or `"default"`.
Only IPv4 is supported by this CLI (dmroute itself is family-agnostic, see
[docs/ip.md](docs/ip.md)).

Example output:

```
$ ip route
default via 192.168.1.1 dev eth0 metric 50
192.168.1.0/24 dev eth0 metric 0 connected
10.0.0.0/8 via 192.168.1.254 dev eth0 metric 100
```

You do not need to `ip route add` an interface's own subnet - dmroute adds
a `connected` route for it automatically as soon as the interface is given
an IP address (see [docs/ip.md](docs/ip.md#automatic-registration), or
dmroute's own docs for the full rationale from dmroute's side).

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
`dmod.h`) rather than depending on `ip` as a linked module - same pattern
as `tools/ifconfig/tests/ifconfig_test.c`, for the same reason (an
Application-type module can't be loaded as another module's dependency).

## Documentation

See the `docs/` directory:

- **[ip.md](docs/ip.md)** - Usage, exit codes, and how this module is
  tested

View documentation using `dmf-man ip`.

## Project Structure

```
ip/
├── docs/
│   ├── README.md
│   └── ip.md
├── src/
│   └── ip.c
├── tests/
│   ├── CMakeLists.txt
│   └── ip_test.c
├── CMakeLists.txt
└── ip.dmr
```

LICENSE is shared with the rest of the `dmnet` repository (`../../LICENSE`).

## Author

Patryk Kubiak

## License

MIT
