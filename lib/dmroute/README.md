# dmroute

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](../../LICENSE)

dmroute DMOD library module - a longest-prefix-match IP routing table.
Every [dmnetif](../dmnetif) interface reports itself here automatically as
soon as it's assigned an IP address (dmnetif calls `dmroute_add()`/
`_remove()` directly - see
[docs/dmroute.md](docs/dmroute.md#automatic-registration)); dmroute itself
has no dependency on any other in-tree module - it owns `dmroute_addr_t`,
the address type dmnetif/dmarp/dmip all build on. Anything else (a TCP/IP
stack, static config, the `ip` CLI) can add further routes through
`dmroute_add()` and ask "which interface should this destination go
through" with `dmroute_lookup()`.

## Building

This module lives under `lib/dmroute` inside the `dmnet` repository and is
built as part of the parent's CMake configure (the top-level
`CMakeLists.txt` calls `add_subdirectory(lib)`, whose own `CMakeLists.txt`
calls `add_subdirectory(dmroute)` first, before any other lib module -
dmroute has no dependency on anything else in this tree) - it is not built
standalone.

```bash
mkdir -p build
cd build
cmake ..
cmake --build . --target dmroute
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub.

## Usage

```c
#include "dmroute.h"
```

## Documentation

See the `docs/` directory:

- **[dmroute.md](docs/dmroute.md)** - Overview and architecture
- **[api-reference.md](docs/api-reference.md)** - Complete API documentation

View documentation using `dmf-man dmroute`.

## Project Structure

```
dmroute/
├── docs/              # Documentation (markdown format)
├── include/           # Public headers
│   └── dmroute.h
├── src/
│   └── dmroute.c
├── tests/
│   ├── CMakeLists.txt
│   └── dmroute_test.c
├── CMakeLists.txt
└── dmroute.dmr
```

LICENSE is shared with the rest of the `dmnet` repository (`../../LICENSE`) -
see `dmroute.dmr` for how it's picked up during packaging.

## Author

Patryk Kubiak

## License

MIT
