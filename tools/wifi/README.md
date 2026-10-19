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
  connect SSID [PASSWORD] -s   ... and save the network, so that wifid connects by itself
  save SSID [PASSWORD]     save a network (not echoed when asked for)
  saved                    the saved networks
  forget SSID              remove a saved network
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

## Saved networks

`wifi save` / `connect -s` / `saved` / `forget` keep the networks in the **"wifi" vault of
dmsecrets** (records `wifi/<SSID>` = the password); the vault is at `DMSECRETS_WIFI_PATH`, else
`DMSECRETS_PATH`. It is opened with the **device key** (`/dev/devkey`), the same way
[wifid](../../services/wifid/README.md) opens it, which is what lets `wifid` connect to them at
every start with nobody to ask. A firmware with no device secret falls back to a passphrase
asked at the terminal - but then `wifid` cannot open that vault. `connect -s` saves a network
only once it has connected (a wrong password is not kept).
