# ip Documentation

Welcome to the ip module documentation.

## Contents

- **[ip.md](ip.md)** - Usage, exit codes, and how this module is tested

## Quick Reference

```
ip route [show]                                                         List all routes
ip route show <dest>                                                    Show the best-matching route for <dest>
ip route get <dest>                                                     Same as 'route show <dest>'
ip route add <dest>[/<prefixlen>] [via <gw>] dev <iface> [metric <n>]   Add a route
ip route del <dest>[/<prefixlen>] dev <iface>                           Remove a route
ip addr [show]                                                          List every interface's address
ip addr show <iface>                                                    Show one interface's address
ip addr add <addr>/<prefixlen> dev <iface>                              Assign a static address
ip --help | -h                                                          Show this help
```

View documentation using `dmf-man`:

```bash
dmf-man ip          # Main documentation
dmf-man ip ip       # ip.md
```
