# wifid

Keeps a Wi-Fi interface connected to a **saved network**, by itself - nobody types a
password after a restart.

```
wifi save "Home Network" mypassword      # once (or: wifi connect "Home Network" -s)
                                         # the networks go to the "wifi" vault of dmsecrets
```

`wifid` is started by libsystemd for **every** interface dmnetif registers
(`configs/wifid.rules`, as for networkd and dhcp) and ends at once on one that is not a
Wi-Fi interface (it asks for `DMDRVI_IOCTL_WIFI_GET_STATUS`). On a Wi-Fi one it stays:

1. when it needs a network, opens the vault of the saved networks (records `wifi/<SSID>` =
   the password) with the **device key** (`DMSECRETS_OPEN_DEVICE_KEY`: a key derived from `/dev/devkey`, which
   dmod-boot serves from the secret it was built with) - nobody is asked for a passphrase;
2. when the interface is not connected, scans, takes the saved network **in range with the
   strongest signal**, **closes the vault again** (a vault is open once per system, so `wifi save`
   works while wifid runs, and the passwords are not kept in memory) and connects to it
   (`DMDRVI_IOCTL_WIFI_CONNECT`);
3. watches the connection; when it has been gone for 30 s it looks again - so the board moves
   from one saved network to another by itself.

Where the vault is: `DMSECRETS_WIFI_PATH`, else `DMSECRETS_PATH` (the board sets one at boot,
e.g. dmod-boot's `DMBOOT_DMSECRETS_PATH`). Without a vault `wifid` says so and waits - the card
may not be there yet. Without the device key (a firmware with no device secret) it cannot open
the vault and ends with an error: the vault must then be unlocked by a person, which a service
cannot do.

Only networks that are **visible** in a scan are used: a hidden SSID is not found. The password
is read into the request, sent, and wiped; nothing is logged but the network's name.

See `configs/` for the unit template and the rule.
