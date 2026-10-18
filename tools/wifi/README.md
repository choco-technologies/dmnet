# wifi

The Wi-Fi of a network device, from the shell. It only opens the device node
and uses the standard `DMDRVI_IOCTL_WIFI_*` commands of dmdrvi, so it works
with every Wi-Fi driver (e.g. `dmesphosted`) - the configuration is dmdevfs's,
the address and routes ifconfig's. See [docs/wifi.md](docs/wifi.md).

```
wifi [-d DEVICE] COMMAND [ARGS]
  status                   the connection (default)
  info                     the driver, radio, firmware, MAC, counters
  scan                     the networks in range
  connect SSID [PASSWORD]  connect (and again whenever lost), wait for it;
                           without PASSWORD it asks (not echoed; Enter: none)
  disconnect               disconnect and forget the network
```

`DEVICE` defaults to `/dev/wlan0`. For example:

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

`connect` waits up to 15 s for the connection; the driver keeps trying
after that (`wifi status`). Whether the network is kept across a restart is the driver's business.
