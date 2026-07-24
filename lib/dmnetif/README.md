# dmnetif

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](../../LICENSE)

dmnetif DMOD library module - the network interface manager. It is the
boundary between devfs (`dmdevfs`/`dmdrvi` device files, e.g.
`/dev/dmeth0`) and the network: a driver registers the devfs path it was
assigned as a named interface (`"eth0"`); everything above that line (a
TCP/IP stack, `netctl`/`ifconfig`) only ever talks to dmnetif by interface
name.

> **Status:** API-only stub - see `src/dmnetif.c` and
> [docs/api-reference.md](docs/api-reference.md).

## Building

This module lives under `lib/dmnetif` inside the `dmnet` repository and is
built as part of the parent's CMake configure (the top-level
`CMakeLists.txt` calls `add_subdirectory(lib)`, whose own `CMakeLists.txt`
calls `add_subdirectory(dmnetif)`) - it is not built standalone.

```bash
mkdir -p build
cd build
cmake ..
cmake --build . --target dmnetif
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub.

## Usage

```c
#include "dmnetif.h"
```

## Documentation

See the `docs/` directory:

- **[dmnetif.md](docs/dmnetif.md)** - Overview and architecture
- **[api-reference.md](docs/api-reference.md)** - Complete API documentation

View documentation using `dmf-man dmnetif`.

## Project Structure

```
dmnetif/
├── docs/              # Documentation (markdown format)
├── include/           # Public headers
│   └── dmnetif.h
├── src/
│   └── dmnetif.c
├── tests/
│   ├── CMakeLists.txt
│   └── dmnetif_test.c
├── CMakeLists.txt
└── dmnetif.dmr
```

LICENSE is shared with the rest of the `dmnet` repository (`../../LICENSE`) -
see `dmnetif.dmr` for how it's picked up during packaging.

## Author

Patryk Kubiak

## License

MIT
