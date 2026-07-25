# dmip

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](../../LICENSE)

dmip DMOD library module - shared IP-layer type definitions
(`dmip_addr_t`). Exists so [dmnetif](../dmnetif) and [dmroute](../dmroute)
can both use the same IP address type without either one depending on the
other for it - see [docs/dmip.md](docs/dmip.md) for why that matters.

Has no public API functions and no runtime state - it's a pure header
dependency (`dmip_if`), never actually loaded by anything that only needs
its types.

## Building

This module lives under `lib/dmip` inside the `dmnet` repository and is
built as part of the parent's CMake configure (the top-level
`CMakeLists.txt` calls `add_subdirectory(lib)`, whose own `CMakeLists.txt`
calls `add_subdirectory(dmip)` before the modules that depend on it) - it
is not built standalone.

```bash
mkdir -p build
cd build
cmake ..
cmake --build . --target dmip
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub.

## Usage

```c
#include "dmip.h"
```

## Documentation

See the `docs/` directory:

- **[dmip.md](docs/dmip.md)** - Overview and rationale
- **[api-reference.md](docs/api-reference.md)** - Type reference

View documentation using `dmf-man dmip`.

## Project Structure

```
dmip/
├── docs/              # Documentation (markdown format)
├── include/           # Public headers
│   └── dmip.h
├── src/
│   └── dmip.c
├── tests/
│   ├── CMakeLists.txt
│   └── dmip_test.c
├── CMakeLists.txt
└── dmip.dmr
```

LICENSE is shared with the rest of the `dmnet` repository (`../../LICENSE`) -
see `dmip.dmr` for how it's picked up during packaging.

## Author

Patryk Kubiak

## License

MIT
