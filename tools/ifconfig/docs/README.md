# ifconfig Documentation

Welcome to the ifconfig module documentation.

## Contents

- **[ifconfig.md](ifconfig.md)** - Usage, exit codes, and how this module is tested

## Quick Reference

```
ifconfig                              List all interfaces
ifconfig --help | -h                  Show this help
ifconfig <iface>                      Show one interface
ifconfig <name> create <device_path>  Register a new interface backed by a devfs node
ifconfig <iface> up                   Bring an interface up
ifconfig <iface> down                 Bring an interface down
ifconfig <iface> mtu <bytes>          Set the interface's MTU
ifconfig <iface> hw ether <mac>       Set the interface's MAC address
ifconfig <iface> broadcast <addr>     Set the interface's IPv4 broadcast address
```

View documentation using `dmf-man`:

```bash
dmf-man ifconfig          # Main documentation
dmf-man ifconfig ifconfig # ifconfig.md
```
