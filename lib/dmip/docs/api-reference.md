# dmip API Reference

See [dmip.md](dmip.md) for the rationale behind this module's design.

## Types

| Type                      | Description                                                                 |
|----------------------------|------------------------------------------------------------------------------|
| `dmip_family_t`           | Re-exported from `dmroute_family_t` (see [dmroute](../../dmroute)): `dmip_family_none` \| `_v4` \| `_v6` |
| `dmip_addr_t`             | Re-exported from `dmroute_addr_t`: `{ family; union { v4[DMIP_IPV4_ADDR_LEN]; v6[DMIP_IPV6_ADDR_LEN]; } addr; }` - one type for both IPv4 and IPv6, discriminated by `family` |
| `dmip_v4_header_t`        | Parsed IPv4 header fields (dscp, ecn, total_length, identification, flag_df, flag_mf, fragment_offset, ttl, protocol, header_checksum, src, dst) |
| `dmip_v6_header_t`        | Parsed IPv6 fixed header fields (traffic_class, flow_label, payload_length, next_header, hop_limit, src, dst) |
| `dmip_v4_fragment_func_t` | `void (*)(const uint8_t* fragment, size_t fragment_len, void* user_data)` - callback for `dmip_v4_fragment()` |
| `dmip_v6_fragment_func_t` | `void (*)(const uint8_t* fragment, size_t fragment_len, void* user_data)` - callback for `dmip_v6_fragment()` |

## Constants

| Constant                     | Value        | Description                    |
|--------------------------------|-------------|---------------------------------|
| `DMIP_IPV4_ADDR_LEN`          | 4           | Length in bytes of an IPv4 address |
| `DMIP_IPV6_ADDR_LEN`          | 16          | Length in bytes of an IPv6 address |
| `DMIP_PROTO_ICMP`             | 1           | Protocol number for ICMP |
| `DMIP_PROTO_TCP`              | 6           | Protocol number for TCP |
| `DMIP_PROTO_UDP`              | 17          | Protocol number for UDP |
| `DMIP_PROTO_IPV6_FRAGMENT`    | 44          | next_header value for an IPv6 Fragment extension header |
| `DMIP_PROTO_ICMPV6`           | 58          | Protocol number for ICMPv6 |
| `DMIP_V4_HEADER_LEN`          | 20          | IPv4 header length (no options) |
| `DMIP_V6_HEADER_LEN`          | 40          | IPv6 fixed header length |
| `DMIP_V6_FRAGMENT_HEADER_LEN` | 8           | IPv6 Fragment extension header length |
| `DMIP_DEFAULT_TTL`            | 64          | Suggested default IPv4 TTL |
| `DMIP_DEFAULT_HOP_LIMIT`      | 64          | Suggested default IPv6 Hop Limit |
| `DMIP_REASSEMBLY_TIMEOUT_MS`  | 30000       | How long an incomplete reassembly is kept before being discarded |

## Functions

### Checksum

| Function | Description |
|----------|--------------|
| `dmip_checksum(data, length)` | RFC 1071 Internet checksum over `data` |

### IPv4

| Function | Description |
|----------|--------------|
| `dmip_v4_build_header(buffer, buffer_len, header)` | Build a 20-byte IPv4 header, checksum included |
| `dmip_v4_parse_header(buffer, length, header, header_len)` | Parse an IPv4 header |
| `dmip_v4_checksum_valid(buffer, length)` | Verify a received IPv4 header's checksum |
| `dmip_v4_decrement_ttl(buffer, length)` | Decrement TTL in place, fixing up the checksum; `-ETIMEDOUT` if the packet must be dropped |
| `dmip_v4_next_identification(void)` | Next value from the system-wide IPv4 identification counter |
| `dmip_v4_fragment(header, payload, payload_len, mtu, callback, user_data)` | Split `payload` into `mtu`-sized IPv4 packets |
| `dmip_v4_reassemble(fragment, length, out_packet, out_length)` | Feed one received IPv4 packet through reassembly |
| `dmip_v4_get_source_address(dst, out_src)` | Find the source address `dmip_v4_send()` would use to reach `dst` (route lookup + the egress interface's own IP) |
| `dmip_v4_send(header, payload, payload_len, arp_timeout_ms)` | Build, fragment (if needed) and transmit a complete IPv4 packet - route lookup, ARP resolution, and `dmnetif_send()` all in one call |
| `dmip_v4_receive(iface, out_packet, out_length)` | Poll `iface` once for an inbound IPv4 packet, running it through reassembly |

### IPv6

| Function | Description |
|----------|--------------|
| `dmip_v6_build_header(buffer, buffer_len, header)` | Build a 40-byte IPv6 fixed header |
| `dmip_v6_parse_header(buffer, length, header)` | Parse an IPv6 fixed header |
| `dmip_v6_decrement_hop_limit(buffer, length)` | Decrement Hop Limit in place; `-ETIMEDOUT` if the packet must be dropped |
| `dmip_v6_next_identification(void)` | Next value from the system-wide IPv6 fragment identification counter |
| `dmip_v6_fragment(header, payload, payload_len, mtu, identification, callback, user_data)` | Split `payload` into `mtu`-sized IPv6 packets, adding a Fragment header if needed |
| `dmip_v6_reassemble(fragment, length, out_packet, out_length)` | Feed one received IPv6 packet through reassembly |
| `dmip_v6_receive(iface, out_packet, out_length)` | Poll `iface` once for an inbound IPv6 packet, running it through reassembly. No `dmip_v6_send()` yet - see [dmip.md](dmip.md#send--receive) |

### Family-agnostic

| Type / Function | Description |
|------------------|--------------|
| `dmip_header_t` | `{ family; union { dmip_v4_header_t v4; dmip_v6_header_t v6; } header; }` - set `family`, fill in the matching union member |
| `dmip_send(header, payload, payload_len, arp_timeout_ms)` | Dispatches to `dmip_v4_send()` for `dmip_family_v4`; `-ENOSYS` for `dmip_family_v6` (no `dmip_v6_send()` yet); `-EINVAL` if `header` is `NULL` or `family` is neither |
| `dmip_receive(iface, out_family, out_packet, out_length)` | One `dmnetif_receive()` call handling either family - see [dmip.md](dmip.md#family-agnostic-dmip_send-dmip_receive) for why this isn't the same as calling `dmip_v4_receive()`/`_v6_receive()` back to back |

See `include/dmip.h` for full parameter/return documentation on every function above.
