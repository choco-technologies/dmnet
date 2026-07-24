# dmnetif API Reference

> **Status:** stub. Every function below compiles and returns a safe
> placeholder (`NULL`/`false`/`0`/`-ENOSYS`) - see `src/dmnetif.c`. This
> reference documents the intended contract, not yet the real behavior.

See [dmnetif.md](dmnetif.md) for the architecture and rationale behind this
API's shape.

## Types

| Type                       | Description                                                       |
|----------------------------|---------------------------------------------------------------------|
| `dmnetif_iface_t`          | Opaque handle to one registered network interface                  |
| `dmnetif_mac_addr_t`       | `{ uint8_t addr[DMNETIF_MAC_ADDR_LEN] }` - MAC address              |
| `dmnetif_link_status_t`    | `dmnetif_link_down` \| `dmnetif_link_up`                            |
| `dmnetif_iterator_func_t`  | `bool (*)(dmnetif_iface_t iface, void* user_data)` - see `dmnetif_for_each()` |

## Registration (driver-facing)

| Function                                    | Description                                                        |
|-----------------------------------------------|-----------------------------------------------------------------------|
| `dmnetif_register(name, device_path)`        | Register a devfs-backed device as a named network interface. Opens `device_path` internally. |
| `dmnetif_unregister(iface)`                  | Bring the interface down, close its device file, remove it. Safe on `NULL`. |

## Lookup / enumeration (consumer-facing)

| Function                                    | Description                                                        |
|-----------------------------------------------|-----------------------------------------------------------------------|
| `dmnetif_find_by_name(name)`                 | Find a registered interface by name.                                |
| `dmnetif_count()`                            | Number of currently registered interfaces.                          |
| `dmnetif_for_each(callback, user_data)`      | Visit every registered interface (stops early if `callback` returns `false`). |
| `dmnetif_get_name(iface)`                    | Interface name, as passed to `dmnetif_register()`.                   |

## State control

| Function                        | Description                                             |
|-----------------------------------|-------------------------------------------------------------|
| `dmnetif_up(iface)`             | Bring the interface up.                                  |
| `dmnetif_down(iface)`           | Bring the interface down.                                |
| `dmnetif_is_up(iface)`          | Whether `dmnetif_up()` has been called without a matching `dmnetif_down()`. |
| `dmnetif_get_link_status(iface)`| Query the current link status from the driver.           |

## MAC address

| Function                                          | Description               |
|------------------------------------------------------|-------------------------------|
| `dmnetif_get_mac_address(iface, mac)`                | Read the interface's MAC address. |
| `dmnetif_set_mac_address(iface, mac)`                | Set the interface's MAC address.  |

## Frame I/O

| Function                                    | Description                                                        |
|-----------------------------------------------|-----------------------------------------------------------------------|
| `dmnetif_send(iface, frame, length)`         | Transmit one frame. Returns bytes actually sent.                     |
| `dmnetif_receive(iface, buffer, size)`       | Receive one frame if available (non-blocking). Returns bytes actually received. |

## Escape hatch

| Function                                    | Description                                                        |
|-----------------------------------------------|-----------------------------------------------------------------------|
| `dmnetif_ioctl(iface, command, arg)`         | Forward a raw ioctl to the interface's underlying device file (driver-specific commands, e.g. `dmeth`'s `DMETH_IOCTL_SET_PROMISCUOUS_MODE`). |
