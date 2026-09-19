# networkd

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

networkd DMOD application module.

## Description

networkd reads one network interface. Each instance runs
`dmnetbridge_handle_netif_rx(iface)` for the interface named on its command
line - the only code path allowed to call `dmnetif_receive()` on that
interface while the instance owns it (see
[dmnetbridge's docs](../../lib/dmnetbridge/docs/dmnetbridge.md)). This is
what lets `dmip`/`dmudp` receive a packet from *any* interface without ever
naming one themselves.

There is one instance per interface, started automatically: `dmnetif`
reports every interface it registers as a `netif` class device, and
[`configs/networkd.rules`](configs/networkd.rules) maps that class to
[`configs/networkd@.ini`](configs/networkd@.ini), so libsystemd instantiates
`networkd@<interface>` for each one. Same shape `console@.ini` has for tty
nodes.

The pump runs on the instance's own stack, not on a thread spawned beside
it: with a single interface to read there is nothing for the process to do
afterwards, so a supervisor thread would only sleep until killed - one extra
stack per interface, bought for nothing. It also means an interface that
appears after boot gets a pump like any other, rather than being missed
because enumeration already happened.

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

networkd takes the interface to pump as its only argument, and runs until
that interface goes away:

```bash
dmod_loader /path/to/networkd.dmf eth0
```

### Starting at boot

Nothing needs to start networkd by hand. Install both files from `configs/`:
[`networkd@.ini`](configs/networkd@.ini) into the directory scanned by
`libsystemd_scan()`, and [`networkd.rules`](configs/networkd.rules) into the
one passed to `libsystemd_load_rules()` (see
[dmsystem's configuration.md](../../../dmsystem/app/libsystemd/docs/configuration.md)
for both formats). From then on every interface `dmnetif` registers starts
its own `networkd@<interface>`.

Note that `networkd@.ini` is a *template*: on its own it starts nothing, so
an installation that omits the rules file gets no pump at all. Conversely
`service start networkd@eth0` works without the rules file, since libsystemd
instantiates a template on demand.

## Documentation

See the `docs/` directory:

- **[api-reference.md](docs/api-reference.md)** - Command-line usage

View documentation using `dmf-man networkd`.

## Project Structure

```
networkd/
├── configs/           # libsystemd unit file (starts networkd at boot)
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
