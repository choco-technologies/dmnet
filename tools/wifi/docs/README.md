# wifi Documentation

The `wifi` module: scan, connect and status of a Wi-Fi network device.

## Contents

- **[wifi.md](wifi.md)** - Usage, how it talks to the driver, how it is tested

## Quick Reference

```
wifi [-d DEVICE] COMMAND [ARGS]
  status                   the connection (default)
  info                     the driver, radio, firmware, MAC, counters
  scan                     the networks in range
  connect SSID [PASSWORD]  connect (and again whenever lost), wait for it
  disconnect               disconnect and forget the network
```

```bash
dmf-man wifi        # Main documentation
dmf-man wifi wifi   # wifi.md
```
