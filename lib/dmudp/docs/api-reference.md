# dmudp API Reference

See [dmudp.md](dmudp.md) for the architecture and rationale behind this
module's design.

## Constants

| Constant          | Value | Description                                    |
|--------------------|-------|--------------------------------------------------|
| `DMUDP_HEADER_LEN` | 8     | UDP header length in bytes (source port, destination port, length, checksum) |

## Checksum

| Function | Description |
|----------|--------------|
| `dmudp_v4_checksum_valid(src_ip, dst_ip, segment, segment_len)` | Verify a received IPv4 UDP segment's checksum against the RFC 768 pseudo-header |
| `dmudp_v6_checksum_valid(src_ip, dst_ip, segment, segment_len)` | Verify a received IPv6 UDP segment's checksum against the RFC 8200 pseudo-header |

## Send / receive

No per-family split (`dmudp_v4_send()`/`_v6_send()`,
`dmudp_v4_receive()`/`_v6_receive()`) - see
[dmudp.md](dmudp.md#one-function-per-direction-not-one-per-family) for why.

| Function | Description |
|----------|--------------|
| `dmudp_send(dst_ip, dst_port, src_port, payload, payload_len, arp_timeout_ms)` | Build, checksum and send a UDP datagram to `dst_ip`. IPv4: calls `dmip_v4_get_source_address()` then `dmip_send()`. IPv6: `-ENOSYS` (no IPv6 send path yet - see [dmudp.md](dmudp.md#no-ipv6-send-yet)) |
| `dmudp_receive(iface, out_family, out_src_ip, out_src_port, out_dst_port, out_payload, out_payload_len)` | Poll `iface` once for an inbound UDP datagram of either family, verifying its checksum (`dmudp_v4_checksum_valid()`, skipped if the wire value is 0 per RFC 768; or `dmudp_v6_checksum_valid()`, always required per RFC 8200) and reporting which family arrived via `*out_family` |

All addresses use `dmip_addr_t` from [dmip](../../dmip); all error codes
returned here are passed straight through from `dmip_send()`/
`dmip_v4_send()` except `-EBADMSG` (checksum mismatch) and
`-EPROTO`/`-EINVAL` for arguments dmudp validates itself - see
`include/dmudp.h` for the full breakdown on each function.
