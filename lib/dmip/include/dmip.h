#ifndef DMIP_H
#define DMIP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmip_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file dmip.h
 * @brief DMOD IP protocol - Public API
 *
 * dmip is the IP layer itself: building and parsing IPv4/IPv6 headers,
 * the IPv4 header checksum, TTL/hop-limit handling, identification
 * generation, and fragmentation/reassembly for both families. It also
 * holds dmip_addr_t, the address type shared by every module that speaks
 * IP (dmnetif tracks one per interface, dmroute matches against one per
 * route) - see "Address type" below.
 *
 * Packets are built/parsed as raw byte buffers rather than packed C
 * structs, same as lib/dmarp/src/dmarp.c and tools/ip/src/ip.c - dmod's
 * minimal module runtime gives no guarantee about struct packing.
 *
 * dmip depends on dmlist (fragment reassembly bookkeeping) and dmosi
 * (mutexes, tick count for reassembly timeouts) - otherwise nothing.
 * There is exactly one reassembly table and one pair of identification
 * counters per system, so every function here is plain Built-in API
 * (dmod_dmip_api), not a DIF/MAL.
 */

/* ============================================================================
 *                      Address type
 * ========================================================================== */

/**
 * @brief IP address family - which member of dmip_addr_t's addr union is valid
 */
typedef enum
{
    dmip_family_none = 0,    /**< No address assigned */
    dmip_family_v4   = 4,    /**< addr.v4 is valid */
    dmip_family_v6   = 6,    /**< addr.v6 is valid */
} dmip_family_t;

/**
 * @brief Length in bytes of an IPv4 address
 */
#define DMIP_IPV4_ADDR_LEN 4

/**
 * @brief Length in bytes of an IPv6 address
 */
#define DMIP_IPV6_ADDR_LEN 16

/**
 * @brief A single IP address, either IPv4 or IPv6
 *
 * One type covering both families (discriminated by `family`) rather than
 * separate dmip_ipv4_addr_t/dmip_ipv6_addr_t types and parallel _v4/_v6
 * function pairs everywhere an address is used - callers branch on
 * `family` once, not per function.
 */
typedef struct
{
    dmip_family_t family;
    union
    {
        uint8_t v4[DMIP_IPV4_ADDR_LEN];
        uint8_t v6[DMIP_IPV6_ADDR_LEN];
    } addr;
} dmip_addr_t;

/* ============================================================================
 *                      Protocol numbers
 * ========================================================================== */

/**
 * @brief Well-known IP protocol/next-header numbers, for the
 *        dmip_v4_header_t.protocol / dmip_v6_header_t.next_header fields
 *
 * Not an exhaustive registry (IANA's is hundreds of entries long) - only
 * the ones dmip itself needs (DMIP_PROTO_IPV6_FRAGMENT, used internally by
 * dmip_v6_fragment()/_reassemble()) plus the handful every caller building
 * a packet on top of dmip will need immediately.
 */
#define DMIP_PROTO_ICMP           1u
#define DMIP_PROTO_TCP            6u
#define DMIP_PROTO_UDP            17u
#define DMIP_PROTO_IPV6_FRAGMENT  44u
#define DMIP_PROTO_ICMPV6         58u

/* ============================================================================
 *                      Common constants
 * ========================================================================== */

/**
 * @brief IPv4 header length in bytes
 *
 * dmip_v4_build_header() never emits options (IHL is always 5) - see
 * docs/dmip.md for why. dmip_v4_parse_header() still honors a larger IHL
 * on a received header (skipping past any options, which are not
 * interpreted), reporting the real length via its `header_len` out param.
 */
#define DMIP_V4_HEADER_LEN 20u

/**
 * @brief IPv6 fixed header length in bytes (extension headers, other than
 *        the Fragment header dmip_v6_fragment()/_reassemble() add/consume
 *        themselves, are not supported)
 */
#define DMIP_V6_HEADER_LEN 40u

/**
 * @brief Length in bytes of the IPv6 Fragment extension header
 *        (RFC 8200 4.5) that dmip_v6_fragment() inserts and
 *        dmip_v6_reassemble() consumes
 */
#define DMIP_V6_FRAGMENT_HEADER_LEN 8u

/**
 * @brief Conventional default TTL (IPv4) / Hop Limit (IPv6) for a
 *        newly-built packet - just a suggested starting value, dmip
 *        itself has no opinion (dmip_v4_build_header()/_v6_build_header()
 *        write whatever the caller put in header->ttl / ->hop_limit)
 */
#define DMIP_DEFAULT_TTL       64u
#define DMIP_DEFAULT_HOP_LIMIT 64u

/**
 * @brief How long an incomplete fragment reassembly is kept before being
 *        silently discarded
 *
 * Applies to both dmip_v4_reassemble() and dmip_v6_reassemble() - if the
 * remaining fragments of a packet don't arrive within this window, the
 * fragments received so far are dropped and their memory freed (checked
 * opportunistically on every _reassemble() call, not by a background
 * timer - see src/dmip.c). 30 seconds mirrors common real-world IP
 * fragment reassembly timeouts.
 */
#define DMIP_REASSEMBLY_TIMEOUT_MS (30u * 1000u)

/* ============================================================================
 *                      Checksum
 * ========================================================================== */

/**
 * @brief RFC 1071 Internet checksum
 *
 * The one primitive both dmip_v4_build_header() (to fill the header
 * checksum field) and dmip_v4_checksum_valid() (to verify it) are built
 * on - exposed publicly since it's also exactly what a future upper-layer
 * module (TCP/UDP) will need for its own pseudo-header checksum.
 *
 * IPv6 has no header checksum of its own (RFC 8200) - upper-layer
 * protocols over IPv6 still use this same primitive, just over a
 * different (caller-assembled) buffer.
 *
 * @param data   Bytes to sum. An odd trailing byte is treated as the high
 *               byte of a final 16-bit word (its low byte zero) - the
 *               standard Internet checksum padding rule.
 * @param length Number of bytes in `data`
 *
 * @return The checksum, ready to place directly into a header's checksum
 *         field
 */
dmod_dmip_api(1.0, uint16_t, _checksum, ( const void* data, size_t length ));

/* ============================================================================
 *                      IPv4
 * ========================================================================== */

/**
 * @brief Parsed IPv4 header fields (RFC 791)
 *
 * dmip_v4_build_header() ignores `header_checksum` (always recomputes it)
 * and always emits a 20-byte header with no options; dmip_v4_parse_header()
 * fills every field, including `header_checksum` with the value as found
 * on the wire (not yet verified - see dmip_v4_checksum_valid()).
 */
typedef struct
{
    uint8_t     dscp;              /**< Differentiated Services Code Point (6 bits, 0-63) */
    uint8_t     ecn;                /**< Explicit Congestion Notification (2 bits, 0-3) */
    uint16_t    total_length;      /**< Header + payload length, bytes */
    uint16_t    identification;    /**< See dmip_v4_next_identification() */
    bool        flag_df;            /**< Don't Fragment */
    bool        flag_mf;            /**< More Fragments */
    uint16_t    fragment_offset;   /**< In 8-byte units (0-8191), matching the wire field directly */
    uint8_t     ttl;
    uint8_t     protocol;          /**< DMIP_PROTO_* */
    uint16_t    header_checksum;   /**< Parse output only - see above */
    dmip_addr_t src;                /**< family must be dmip_family_v4 */
    dmip_addr_t dst;                /**< family must be dmip_family_v4 */
} dmip_v4_header_t;

/**
 * @brief Build a 20-byte IPv4 header (no options) into `buffer`, computing
 *        and filling in its checksum
 *
 * @param buffer     Output buffer, must be at least DMIP_V4_HEADER_LEN bytes
 * @param buffer_len Size of `buffer` in bytes
 * @param header     Fields to encode. `header->src`/`->dst` must both have
 *                    family dmip_family_v4. `header->header_checksum` is
 *                    ignored (always recomputed)
 *
 * @return 0 on success, -EINVAL on a NULL argument, too-small buffer, or a
 *         non-IPv4 address family
 */
dmod_dmip_api(1.0, int, _v4_build_header, ( uint8_t* buffer, size_t buffer_len, const dmip_v4_header_t* header ));

/**
 * @brief Parse an IPv4 header from `buffer`
 *
 * Does not verify the header checksum - see dmip_v4_checksum_valid() for
 * that, kept separate so a caller that doesn't care (e.g. building a
 * simple test fixture) never pays for it.
 *
 * @param buffer     Received bytes, header first
 * @param length     Number of valid bytes in `buffer`
 * @param header     Output: parsed fields
 * @param header_len Output: the header's real length in bytes (IHL * 4,
 *                    which may exceed DMIP_V4_HEADER_LEN if the header
 *                    carries options - those are skipped, not parsed).
 *                    `buffer + *header_len` is where the payload starts
 *
 * @return 0 on success, -EINVAL on a NULL argument or `length` shorter
 *         than DMIP_V4_HEADER_LEN, -EPROTO if `buffer` is not a
 *         well-formed IPv4 header (bad version, IHL < 5, or a declared
 *         total_length inconsistent with IHL/`length`)
 */
dmod_dmip_api(1.0, int, _v4_parse_header, ( const uint8_t* buffer, size_t length, dmip_v4_header_t* header, size_t* header_len ));

/**
 * @brief Check whether a received IPv4 header's checksum is valid
 *
 * @param buffer Received bytes, header first (as dmip_v4_parse_header())
 * @param length Number of valid bytes in `buffer`
 *
 * @return true if the header checksum matches its contents
 */
dmod_dmip_api(1.0, bool, _v4_checksum_valid, ( const uint8_t* buffer, size_t length ));

/**
 * @brief Decrement a wire-format IPv4 header's TTL in place, fixing up
 *        its checksum to match
 *
 * For a router (dmroute-driven forwarding path) relaying a packet: TTL is
 * decremented directly in the already-built frame rather than requiring a
 * full parse/rebuild round trip.
 *
 * @param buffer Wire-format packet, header first (mutated in place)
 * @param length Number of valid bytes in `buffer`
 *
 * @return 0 if the packet may still be forwarded (TTL is now >= 1),
 *         -ETIMEDOUT if TTL was already 0 or reached 0 after decrementing
 *         (the packet must be dropped, not forwarded - `buffer` is left
 *         unmodified in the already-0 case), -EINVAL on a NULL buffer or
 *         `length` shorter than DMIP_V4_HEADER_LEN, -EPROTO if the
 *         header's own declared length is inconsistent with `length`
 */
dmod_dmip_api(1.0, int, _v4_decrement_ttl, ( uint8_t* buffer, size_t length ));

/**
 * @brief Get the next IPv4 identification value
 *
 * Backed by a single system-wide counter (mutex-guarded, wraps on
 * overflow) - good enough uniqueness for dmip_v4_fragment()'s "all
 * fragments of one packet share an identification" requirement without
 * needing per-flow state.
 *
 * @return The next identification value (never a promise of global
 *         uniqueness across a 2^16 window, same caveat every real IPv4
 *         stack's identification counter has)
 */
dmod_dmip_api(1.0, uint16_t, _v4_next_identification, ( void ));

/**
 * @brief Callback invoked once per fragment by dmip_v4_fragment()
 *
 * @param fragment     One complete wire-format IPv4 packet (header + this
 *                      fragment's payload slice) - only valid for the
 *                      duration of the call, dmip_v4_fragment() reuses/frees
 *                      the buffer immediately after
 * @param fragment_len Length of `fragment` in bytes
 * @param user_data    Passed through from dmip_v4_fragment() unchanged
 */
typedef void (*dmip_v4_fragment_func_t)( const uint8_t* fragment, size_t fragment_len, void* user_data );

/**
 * @brief Split `payload` into one or more complete IPv4 packets no larger
 *        than `mtu` bytes, emitting each via `callback`
 *
 * If `payload_len` already fits in one packet within `mtu`, exactly one
 * (unfragmented) packet is emitted. `header->identification` is used
 * as-is for every fragment - pass a value from dmip_v4_next_identification()
 * unless you have a specific reason to reuse one. `header->flag_mf` and
 * `->fragment_offset` are ignored (recomputed per fragment); every other
 * field of `header` is copied into every fragment unchanged.
 *
 * @param header      Template header - src/dst/protocol/ttl/dscp/ecn/
 *                     identification/flag_df, must have dmip_family_v4
 *                     addresses
 * @param payload     Data to fragment (the upper-layer payload, not
 *                     including any IP header)
 * @param payload_len Length of `payload` in bytes
 * @param mtu         Maximum bytes per emitted packet, header included.
 *                     Must be at least DMIP_V4_HEADER_LEN + 8 (the
 *                     smallest possible non-final fragment)
 * @param callback    Invoked once per emitted packet, in order
 * @param user_data   Passed through to `callback` unchanged
 *
 * @return 0 on success, -EINVAL on a bad argument (including `mtu` too
 *         small to make progress, or a non-IPv4 header address family),
 *         -EMSGSIZE if `payload` doesn't fit in one `mtu`-sized packet and
 *         `header->flag_df` is set, -ENOMEM if the internal fragment
 *         buffer could not be allocated
 */
dmod_dmip_api(1.0, int, _v4_fragment, ( const dmip_v4_header_t* header, const void* payload, size_t payload_len, uint16_t mtu, dmip_v4_fragment_func_t callback, void* user_data ));

/**
 * @brief Feed one received IPv4 packet through fragment reassembly
 *
 * Safe to call on every inbound IPv4 packet unconditionally: an
 * already-whole packet (flag_mf false and fragment_offset 0) is
 * recognized immediately and handed back as-is, with no reassembly
 * bookkeeping - genuine fragments are the only case that touches the
 * reassembly table (keyed by source, destination, protocol and
 * identification, per RFC 791).
 *
 * @param fragment   One received wire-format IPv4 packet
 * @param length     Number of valid bytes in `fragment`
 * @param out_packet Output: on 0, a heap-allocated complete wire-format
 *                    IPv4 packet (header rebuilt with flag_mf/
 *                    fragment_offset cleared and the checksum recomputed,
 *                    followed by the reassembled payload). Owned by the
 *                    caller - release with Dmod_Free() when done
 * @param out_length Output: length of `*out_packet` in bytes
 *
 * @return 0 if `fragment` completed a packet (`*out_packet`/`*out_length`
 *         set), -EINPROGRESS if `fragment` was accepted but the packet is
 *         still incomplete (nothing written to the output params - more
 *         fragments are needed), -EINVAL/-EPROTO if `fragment` itself
 *         isn't a well-formed IPv4 packet (see dmip_v4_parse_header()),
 *         -ENOMEM if a required allocation failed
 */
dmod_dmip_api(1.0, int, _v4_reassemble, ( const uint8_t* fragment, size_t length, uint8_t** out_packet, size_t* out_length ));

/* ============================================================================
 *                      IPv6
 * ========================================================================== */

/**
 * @brief Parsed IPv6 fixed header fields (RFC 8200)
 *
 * Unlike IPv4 there is no header checksum (RFC 8200 removed it) and no
 * fragmentation state in the fixed header itself - fragmentation instead
 * uses a separate extension header, entirely handled by
 * dmip_v6_fragment()/dmip_v6_reassemble() (see those for details); this
 * struct only ever reflects the 40-byte fixed header.
 */
typedef struct
{
    uint8_t     traffic_class;
    uint32_t    flow_label;        /**< 20 bits (0-0xFFFFF) */
    uint16_t    payload_length;    /**< Bytes following the fixed header (extension headers included) */
    uint8_t     next_header;       /**< DMIP_PROTO_* of the first header/protocol following this one */
    uint8_t     hop_limit;
    dmip_addr_t src;                /**< family must be dmip_family_v6 */
    dmip_addr_t dst;                /**< family must be dmip_family_v6 */
} dmip_v6_header_t;

/**
 * @brief Build a 40-byte IPv6 fixed header into `buffer`
 *
 * @param buffer     Output buffer, must be at least DMIP_V6_HEADER_LEN bytes
 * @param buffer_len Size of `buffer` in bytes
 * @param header     Fields to encode. `header->src`/`->dst` must both have
 *                    family dmip_family_v6
 *
 * @return 0 on success, -EINVAL on a NULL argument, too-small buffer, or a
 *         non-IPv6 address family
 */
dmod_dmip_api(1.0, int, _v6_build_header, ( uint8_t* buffer, size_t buffer_len, const dmip_v6_header_t* header ));

/**
 * @brief Parse an IPv6 fixed header from `buffer`
 *
 * @param buffer Received bytes, header first
 * @param length Number of valid bytes in `buffer`
 * @param header Output: parsed fields
 *
 * @return 0 on success, -EINVAL on a NULL argument or `length` shorter
 *         than DMIP_V6_HEADER_LEN, -EPROTO if `buffer` is not a
 *         well-formed IPv6 header (bad version, or a declared
 *         payload_length longer than `length` allows)
 */
dmod_dmip_api(1.0, int, _v6_parse_header, ( const uint8_t* buffer, size_t length, dmip_v6_header_t* header ));

/**
 * @brief Decrement a wire-format IPv6 packet's Hop Limit in place
 *
 * IPv6's equivalent of dmip_v4_decrement_ttl() - simpler, since there's no
 * checksum to fix up afterward.
 *
 * @param buffer Wire-format packet, header first (mutated in place)
 * @param length Number of valid bytes in `buffer`
 *
 * @return 0 if the packet may still be forwarded (Hop Limit is now >= 1),
 *         -ETIMEDOUT if Hop Limit was already 0 or reached 0 after
 *         decrementing (the packet must be dropped), -EINVAL on a NULL
 *         buffer or `length` shorter than DMIP_V6_HEADER_LEN
 */
dmod_dmip_api(1.0, int, _v6_decrement_hop_limit, ( uint8_t* buffer, size_t length ));

/**
 * @brief Get the next IPv6 fragment identification value
 *
 * IPv6's equivalent of dmip_v4_next_identification(), just 32 bits wide
 * (RFC 8200 4.5) - only meaningful when actually fragmenting, see the
 * `identification` parameter of dmip_v6_fragment().
 */
dmod_dmip_api(1.0, uint32_t, _v6_next_identification, ( void ));

/**
 * @brief Callback invoked once per fragment by dmip_v6_fragment()
 *
 * @param fragment     One complete wire-format IPv6 packet (fixed header,
 *                      a Fragment extension header if this call actually
 *                      needed to fragment, and this fragment's payload
 *                      slice) - only valid for the duration of the call
 * @param fragment_len Length of `fragment` in bytes
 * @param user_data    Passed through from dmip_v6_fragment() unchanged
 */
typedef void (*dmip_v6_fragment_func_t)( const uint8_t* fragment, size_t fragment_len, void* user_data );

/**
 * @brief Split `payload` into one or more complete IPv6 packets no larger
 *        than `mtu` bytes, emitting each via `callback`
 *
 * If `payload_len` already fits in one packet within `mtu`, exactly one
 * unfragmented packet (fixed header only, `identification` unused) is
 * emitted. Otherwise every emitted packet carries a Fragment extension
 * header (`header->next_header` becomes the fixed header's next_header
 * for every fragment, with the original next_header moved into the
 * Fragment header - RFC 8200 4.5) using `identification` for all of them.
 * Unlike IPv4, IPv6 has no Don't-Fragment flag - a source is always
 * allowed to fragment its own traffic.
 *
 * @param header          Template fixed header - src/dst/next_header/
 *                         hop_limit/traffic_class/flow_label, must have
 *                         dmip_family_v6 addresses
 * @param payload         Data to fragment
 * @param payload_len     Length of `payload` in bytes
 * @param mtu             Maximum bytes per emitted packet. Must be large
 *                         enough for the fixed header, a Fragment header,
 *                         and at least 8 bytes of payload if fragmenting
 *                         is actually needed
 * @param identification  Shared across every fragment of this packet -
 *                         use dmip_v6_next_identification(). Ignored if
 *                         `payload` fits in one packet
 * @param callback        Invoked once per emitted packet, in order
 * @param user_data       Passed through to `callback` unchanged
 *
 * @return 0 on success, -EINVAL on a bad argument (including `mtu` too
 *         small to make progress, or a non-IPv6 header address family),
 *         -ENOMEM if the internal fragment buffer could not be allocated
 */
dmod_dmip_api(1.0, int, _v6_fragment, ( const dmip_v6_header_t* header, const void* payload, size_t payload_len, uint16_t mtu, uint32_t identification, dmip_v6_fragment_func_t callback, void* user_data ));

/**
 * @brief Feed one received IPv6 packet through fragment reassembly
 *
 * IPv6's equivalent of dmip_v4_reassemble(): safe to call on every
 * inbound IPv6 packet unconditionally. A packet whose next_header isn't
 * DMIP_PROTO_IPV6_FRAGMENT is handed back as-is; a genuine fragment is
 * tracked in the reassembly table (keyed by source, destination, the
 * fragment's original next_header, and its 32-bit identification).
 *
 * @param fragment   One received wire-format IPv6 packet
 * @param length     Number of valid bytes in `fragment`
 * @param out_packet Output: on 0, a heap-allocated complete wire-format
 *                    IPv6 packet (fixed header with next_header restored
 *                    to the original protocol and no Fragment header,
 *                    followed by the reassembled payload). Owned by the
 *                    caller - release with Dmod_Free() when done
 * @param out_length Output: length of `*out_packet` in bytes
 *
 * @return 0 if `fragment` completed a packet, -EINPROGRESS if accepted
 *         but the packet is still incomplete, -EINVAL/-EPROTO if
 *         `fragment` isn't well-formed (see dmip_v6_parse_header()) or
 *         its Fragment header is truncated, -ENOMEM if a required
 *         allocation failed
 */
dmod_dmip_api(1.0, int, _v6_reassemble, ( const uint8_t* fragment, size_t length, uint8_t** out_packet, size_t* out_length ));

#ifdef __cplusplus
}
#endif

#endif // DMIP_H
