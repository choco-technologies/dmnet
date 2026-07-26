# networkd

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

networkd DMOD application module.

## Description

networkd owns reading every network interface. It spawns one dedicated
thread per interface already registered with `dmnetif` at startup, each
running `dmnetbridge_handle_netif_rx(iface)` - the only code path allowed
to call `dmnetif_receive()` on a given interface once this service owns
it (see [dmnetbridge's docs](../../lib/dmnetbridge/docs/dmnetbridge.md)).
This is what lets `dmip`/`dmudp` receive a packet from *any* interface
without ever naming one themselves.

## Building

### Using CMake

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub.

### Using Make

```bash
make DMOD_MODE=DMOD_MODULE DMOD_DIR=/path/to/dmod
```

## Usage

This application module can be loaded and executed using the DMOD loader:

```bash
dmod_loader /path/to/networkd.dmf
```

## Documentation

See the `docs/` directory:

- **[api-reference.md](docs/api-reference.md)** - Command-line usage

View documentation using `dmf-man networkd`.

## Project Structure

```
networkd/
├── docs/              # Documentation (markdown format)
├── src/
│   └── networkd.c
├── tests/
│   ├── CMakeLists.txt
│   └── networkd_test.c
├── CMakeLists.txt
├── Makefile
├── networkd.dmr
└── manifest.dmm
```

## Author

Patryk Kubiak

## License

MIT
