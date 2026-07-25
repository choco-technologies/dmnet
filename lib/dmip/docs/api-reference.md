# dmip API Reference

See [dmip.md](dmip.md) for the rationale behind this module's existence.

## Types

| Type                | Description                                                                 |
|---------------------|------------------------------------------------------------------------------|
| `dmip_family_t`     | `dmip_family_none` \| `_v4` \| `_v6`                                        |
| `dmip_addr_t`       | `{ family; union { v4[DMIP_IPV4_ADDR_LEN]; v6[DMIP_IPV6_ADDR_LEN]; } addr; }` - one type for both IPv4 and IPv6, discriminated by `family` |

## Constants

| Constant             | Value | Description                    |
|-----------------------|-------|---------------------------------|
| `DMIP_IPV4_ADDR_LEN`  | 4     | Length in bytes of an IPv4 address |
| `DMIP_IPV6_ADDR_LEN`  | 16    | Length in bytes of an IPv6 address |

## Functions

_(none - dmip is types only, see [dmip.md](dmip.md#no-runtime-behavior))_
