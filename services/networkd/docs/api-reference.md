# networkd API Reference

networkd has no command-line arguments and no public API of its own - it
is a service, not a library. See [README.md](README.md) for what it does
and `src/networkd.c` for the implementation.

## Usage

```bash
dmod_loader networkd.dmf
```

## Arguments

None.

## Lifecycle

- On start: calls `dmnetbridge_reset()`, then spawns one thread per
  interface already registered with `dmnetif` (via `dmnetif_for_each()`),
  each running `dmnetbridge_handle_netif_rx(iface)`.
- On `dmod_signal()`: stops its main loop, then kills/joins every pump
  thread that hasn't already returned on its own (i.e. whose interface is
  still present) and returns.

## Known limitation

Only interfaces already registered when networkd starts get a pump
thread - `dmnetif` has no "a new interface was just registered"
notification today (unlike `dmdevfs`'s hot-plug callback mechanism for
devices), so an interface registered after networkd has started is not
automatically picked up. Follow-up work, not solved here.
