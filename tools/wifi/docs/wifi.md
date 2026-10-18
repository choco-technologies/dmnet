# wifi

## Overview

`wifi` is the shell's Wi-Fi: it opens the device node of a Wi-Fi network
driver and uses the standard `DMDRVI_IOCTL_WIFI_*` commands of
[dmdrvi](https://github.com/choco-technologies/dmdrvi) (0x700 range). It knows
no driver - any one that implements the contract (e.g. `dmesphosted`) works
the same. The network itself (address, routes, DHCP) is
[ifconfig](../../ifconfig)'s and `networkd`'s, as for any interface.

## Usage

```
wifi [-d DEVICE] COMMAND [ARGS]
  status                   the connection (default)
  info                     the driver, radio, firmware, MAC, counters
  scan                     the networks in range
  connect SSID [PASSWORD]  connect (and again whenever lost), wait for it;
                           without PASSWORD it asks (not echoed; Enter: none)
  disconnect               disconnect and forget the network
```

`DEVICE` defaults to `/dev/wlan0`.

```
> wifi scan
scanning...
SSID                               dBm  ch  security    access point
Home Network                       -47   6  WPA2        f4:ec:38:11:22:33
Cafe                               -80  11  open        00:00:00:00:00:00
2 network(s)
> wifi connect "Home Network"
Password (Enter: none):
connecting to Home Network...
state:   connected
network: Home Network
access point: f4:ec:38:11:22:33
channel: 6, signal -48 dBm
```

`connect` waits up to 15 s for the connection; the driver keeps trying after
that (`wifi status`). Whether the network is kept across a restart is the
driver's business (dmesphosted does not keep it).

## Exit codes

`0` on success; `-1` for a bad command line or a device that cannot be
opened; otherwise the (negative) result of the failed ioctl - `-ENOSYS` when
the node has no Wi-Fi driver behind it.

## Testing

`test_wifi` runs the module through `Dmod_RunModule()` with no Wi-Fi driver
loaded and checks the command line and that failures are reported.
