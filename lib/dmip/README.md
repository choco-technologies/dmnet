# dmip

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](../../LICENSE)

dmip DMOD library module - the IP layer: building/parsing IPv4 and IPv6
headers, the IPv4 header checksum, TTL/Hop-Limit handling, identification
generation, fragmentation/reassembly for both families, and sending/
receiving actual packets (route lookup via [dmroute](../dmroute), ARP via
[dmarp](../dmarp), frame I/O via [dmnetif](../dmnetif)). `dmip_addr_t` is
re-exported from dmroute, which owns the real definition - see
[docs/dmip.md](docs/dmip.md) for the full rationale.

## Building

This module lives under `lib/dmip` inside the `dmnet` repository and is
built as part of the parent's CMake configure (the top-level
`CMakeLists.txt` calls `add_subdirectory(lib)`, whose own `CMakeLists.txt`
calls `add_subdirectory(dmip)` after dmroute/dmnetif/dmarp, the modules it
depends on) - it is not built standalone.

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

dmip_v4_header_t header = {
    .ttl = DMIP_DEFAULT_TTL,
    .protocol = DMIP_PROTO_UDP,
    .identification = dmip_v4_next_identification(),
    .src = my_addr,
    .dst = peer_addr,
};

uint8_t packet[DMIP_V4_HEADER_LEN + sizeof(payload)];
header.total_length = sizeof(packet);
dmip_v4_build_header(packet, sizeof(packet), &header);
memcpy(packet + DMIP_V4_HEADER_LEN, payload, sizeof(payload));
```

## Documentation

See the `docs/` directory:

- **[dmip.md](docs/dmip.md)** - Overview and rationale
- **[api-reference.md](docs/api-reference.md)** - Full API reference

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
