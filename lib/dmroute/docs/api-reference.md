# dmroute API Reference

See [dmroute.md](dmroute.md) for the architecture and rationale behind
this API's shape.

## Types

| Type                       | Description                                                       |
|----------------------------|---------------------------------------------------------------------|
| `dmroute_route_t`          | Opaque handle to one routing table entry                           |
| `dmroute_origin_t`         | `dmroute_origin_static` \| `dmroute_origin_connected` - how a route came to exist |
| `dmroute_iterator_func_t`  | `bool (*)(dmroute_route_t route, void* user_data)` - see `dmroute_for_each()` |
| `dmroute_family_t`         | `dmroute_family_none` \| `_v4` \| `_v6`                             |
| `dmroute_addr_t`           | `{ family; union { v4[DMROUTE_IPV4_ADDR_LEN]; v6[DMROUTE_IPV6_ADDR_LEN]; } addr; }` - one type for both IPv4 and IPv6, discriminated by `family` |

All addresses (`destination`/`netmask`/`gateway`) use `dmroute_addr_t`,
defined by dmroute itself (`dmip.h` re-exports the same type as
`dmip_addr_t` for anything built on top of dmip).

## Add / remove

| Function                                                                   | Description                                                        |
|-------------------------------------------------------------------------------|-----------------------------------------------------------------------|
| `dmroute_add(destination, netmask, gateway, iface_name, metric, origin)`     | Add a route. `destination` is masked with `netmask` internally before storing. `gateway` may be `NULL` for a directly-connected/on-link route. `iface_name` is **not validated** - dmroute has no dependency on dmnetif to check it with (see [dmroute.md](dmroute.md#no-dmnetif-dependency)); callers that care should check with `dmnetif_find_by_name()` first. `origin` is purely informational - pass `dmroute_origin_static` unless you're dmnetif itself. |
| `dmroute_remove(route)`                                                       | Remove a route. Safe on `NULL`.                                     |

## Lookup / enumeration

| Function                                    | Description                                                        |
|-----------------------------------------------|-----------------------------------------------------------------------|
| `dmroute_lookup(destination_ip)`             | Longest-prefix-match lookup; ties broken by lowest metric. `NULL` if nothing matches. |
| `dmroute_count()`                            | Number of routes currently in the table.                            |
| `dmroute_for_each(callback, user_data)`      | Visit every route (stops early if `callback` returns `false`).      |

## Accessors

| Function                                    | Description                                                        |
|-----------------------------------------------|-----------------------------------------------------------------------|
| `dmroute_get_destination(route, destination)`| Destination network address (already masked with the route's netmask). |
| `dmroute_get_netmask(route, netmask)`        | The route's netmask.                                                 |
| `dmroute_get_gateway(route, gateway)`        | Next-hop gateway; `family` is `dmroute_family_none` for a directly-connected/on-link route. |
| `dmroute_get_iface_name(route)`              | Egress interface name. Never validated by dmroute itself - resolve with `dmnetif_find_by_name()` before using it to send anything. |
| `dmroute_get_metric(route)`                  | The route's metric.                                                  |
| `dmroute_get_origin(route)`                  | `dmroute_origin_static` (added via an explicit `dmroute_add()` call) or `dmroute_origin_connected` (added by dmnetif because an interface got an IP address). |
