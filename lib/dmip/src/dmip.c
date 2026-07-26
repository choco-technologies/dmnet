/**
 * @file dmip.c
 * @brief DMOD IP protocol - Implementation
 *
 * Five loosely-coupled pieces live here:
 *
 *  - The RFC 1071 checksum primitive (dmip_checksum()), used by the IPv4
 *    header build/verify/TTL-decrement functions - IPv6 has no header
 *    checksum of its own (RFC 8200).
 *
 *  - IPv4/IPv6 header build/parse, each as a direct byte-buffer <->
 *    struct conversion (no packed C structs - see dmarp.c's own top
 *    comment for why), plus TTL/Hop-Limit decrement and identification
 *    counters.
 *
 *  - Fragmentation (stateless: dmip_v4_fragment()/dmip_v6_fragment() just
 *    slice a payload and emit packets via callback) and reassembly
 *    (stateful: a single system-wide table of in-progress reassemblies,
 *    keyed by family + a packed (addresses, protocol, identification)
 *    key, guarded by g_reassembly_mutex - see "Reassembly" below).
 *
 *  - Send (dmip_v4_send()): the only part of dmip that still reaches
 *    outside dmip itself for I/O, and only as far as dmnetbridge -
 *    dmip_v4_get_source_address()/dmip_v4_send() no longer touch
 *    dmroute/dmarp/dmnetif directly, they call
 *    dmnetbridge_get_source_address()/dmnetbridge_send() once per
 *    fragment. dmip only ever deals with IP addresses now; dmnetbridge
 *    owns routing, ARP resolution, and the actual frame transmit.
 *
 *  - Receive: push-based, not polled. dmip implements dmnetbridge's
 *    packet_received DIF (dmip_dmnetbridge_packet_received() below) -
 *    called by dmnetbridge_handle_netif_rx() for every frame it reads off
 *    any interface, from whatever thread is pumping that interface. It
 *    checks the ethertype, strips the 14-byte L2 header, and feeds the
 *    rest through the existing dmip_v4_reassemble()/_v6_reassemble(). A
 *    completed packet is then dispatched by protocol number - see
 *    "Protocol dispatch" below - to whichever module registered for it
 *    (dmip_register_protocol()), a registered default handler, or
 *    dropped if nobody claimed it. There is no generic "give me the next
 *    packet" pull API anymore (dmip_v4_receive()/_v6_receive()/_receive()
 *    were removed): a single shared queue for every protocol meant two
 *    different protocol consumers polling it could steal each other's
 *    packets - see docs/dmip.md.
 */
#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmip.h"
#include "dmnetbridge.h"
#include "dmnetif.h"
#include "dmlist.h"
#include "dmosi.h"
#include <string.h>
#include <errno.h>

/* ---- Byte helpers ---- */

static void write_u16_be(uint8_t* p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)(value & 0xFFu);
}

static uint16_t read_u16_be(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static void write_u32_be(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)(value & 0xFFu);
}

static uint32_t read_u32_be(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* ---- Module state ---- */

/**
 * @brief Table of in-progress fragment reassemblies (struct
 *        dmip_reassembly_entry*) - see the "Reassembly" section below
 */
static dmlist_context_t* g_reassembly = NULL;
static dmosi_mutex_t     g_reassembly_mutex = NULL;

/**
 * @brief Guards g_v4_next_id/g_v6_next_id
 */
static dmosi_mutex_t g_id_mutex = NULL;
static uint16_t      g_v4_next_id = 0;
static uint32_t      g_v6_next_id = 0;

/**
 * @brief One registered protocol -> handler mapping (struct
 *        dmip_protocol_entry*) - see "Protocol dispatch" below
 */
struct dmip_protocol_entry
{
    uint8_t                  protocol;
    dmip_protocol_handler_t  handler;
};

/**
 * @brief Every currently registered dmip_protocol_entry*, guarded by
 *        g_protocol_mutex
 */
static dmlist_context_t* g_protocol_handlers = NULL;
static dmosi_mutex_t     g_protocol_mutex = NULL;

/**
 * @brief The single registered default handler (dmip_register_default_protocol()),
 *        or NULL if none - guarded by g_protocol_mutex
 */
static dmip_protocol_handler_t g_default_handler = NULL;

/* ---- Checksum ---- */

/**
 * @brief Implementation of dmip_checksum() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, uint16_t, _checksum, ( const void* data, size_t length ))
{
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t sum = 0;

    while (length > 1)
    {
        sum += ((uint32_t)bytes[0] << 8) | (uint32_t)bytes[1];
        bytes += 2;
        length -= 2;
    }
    if (length == 1)
    {
        sum += (uint32_t)bytes[0] << 8;
    }

    while ((sum >> 16) != 0)
    {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

/* ============================================================================
 *                      IPv4
 * ========================================================================== */

/**
 * @brief Implementation of dmip_v4_build_header() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, int, _v4_build_header, ( uint8_t* buffer, size_t buffer_len, const dmip_v4_header_t* header ))
{
    if (buffer == NULL || header == NULL || buffer_len < DMIP_V4_HEADER_LEN)
        return -EINVAL;
    if (header->src.family != dmip_family_v4 || header->dst.family != dmip_family_v4)
        return -EINVAL;

    memset(buffer, 0, DMIP_V4_HEADER_LEN);
    buffer[0] = (uint8_t)((4u << 4) | 5u); /* version 4, IHL 5 (no options) */
    buffer[1] = (uint8_t)((header->dscp << 2) | (header->ecn & 0x03u));
    write_u16_be(&buffer[2], header->total_length);
    write_u16_be(&buffer[4], header->identification);

    uint16_t flags_and_offset = (uint16_t)((header->flag_df ? 0x4000u : 0u) |
                                            (header->flag_mf ? 0x2000u : 0u) |
                                            (header->fragment_offset & 0x1FFFu));
    write_u16_be(&buffer[6], flags_and_offset);

    buffer[8] = header->ttl;
    buffer[9] = header->protocol;
    /* buffer[10..11] (checksum) left zero for the sum below */
    memcpy(&buffer[12], header->src.addr.v4, DMIP_IPV4_ADDR_LEN);
    memcpy(&buffer[16], header->dst.addr.v4, DMIP_IPV4_ADDR_LEN);

    write_u16_be(&buffer[10], dmip_checksum(buffer, DMIP_V4_HEADER_LEN));
    return 0;
}

/**
 * @brief Implementation of dmip_v4_parse_header() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, int, _v4_parse_header, ( const uint8_t* buffer, size_t length, dmip_v4_header_t* header, size_t* header_len ))
{
    if (buffer == NULL || header == NULL || header_len == NULL || length < DMIP_V4_HEADER_LEN)
        return -EINVAL;

    uint8_t version = buffer[0] >> 4;
    uint8_t ihl = buffer[0] & 0x0Fu;
    size_t hlen = (size_t)ihl * 4u;
    if (version != 4u || ihl < 5u || hlen > length)
        return -EPROTO;

    uint16_t flags_and_offset = read_u16_be(&buffer[6]);

    header->dscp = buffer[1] >> 2;
    header->ecn = buffer[1] & 0x03u;
    header->total_length = read_u16_be(&buffer[2]);
    header->identification = read_u16_be(&buffer[4]);
    header->flag_df = (flags_and_offset & 0x4000u) != 0;
    header->flag_mf = (flags_and_offset & 0x2000u) != 0;
    header->fragment_offset = flags_and_offset & 0x1FFFu;
    header->ttl = buffer[8];
    header->protocol = buffer[9];
    header->header_checksum = read_u16_be(&buffer[10]);
    header->src.family = dmip_family_v4;
    memcpy(header->src.addr.v4, &buffer[12], DMIP_IPV4_ADDR_LEN);
    header->dst.family = dmip_family_v4;
    memcpy(header->dst.addr.v4, &buffer[16], DMIP_IPV4_ADDR_LEN);

    if (header->total_length < hlen || (size_t)header->total_length > length)
        return -EPROTO;

    *header_len = hlen;
    return 0;
}

/**
 * @brief Implementation of dmip_v4_checksum_valid() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, bool, _v4_checksum_valid, ( const uint8_t* buffer, size_t length ))
{
    if (buffer == NULL || length < DMIP_V4_HEADER_LEN)
        return false;

    size_t hlen = (size_t)(buffer[0] & 0x0Fu) * 4u;
    if (hlen < DMIP_V4_HEADER_LEN || hlen > length)
        return false;

    return dmip_checksum(buffer, hlen) == 0;
}

/**
 * @brief Implementation of dmip_v4_decrement_ttl() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, int, _v4_decrement_ttl, ( uint8_t* buffer, size_t length ))
{
    if (buffer == NULL || length < DMIP_V4_HEADER_LEN)
        return -EINVAL;

    size_t hlen = (size_t)(buffer[0] & 0x0Fu) * 4u;
    if (hlen < DMIP_V4_HEADER_LEN || hlen > length)
        return -EPROTO;

    if (buffer[8] == 0)
        return -ETIMEDOUT;

    buffer[8] -= 1;
    write_u16_be(&buffer[10], 0);
    write_u16_be(&buffer[10], dmip_checksum(buffer, hlen));

    return (buffer[8] == 0) ? -ETIMEDOUT : 0;
}

/**
 * @brief Implementation of dmip_v4_next_identification() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, uint16_t, _v4_next_identification, ( void ))
{
    dmosi_mutex_lock(g_id_mutex);
    uint16_t id = ++g_v4_next_id;
    dmosi_mutex_unlock(g_id_mutex);
    return id;
}

/**
 * @brief Emit a single unfragmented IPv4 packet - the "payload already
 *        fits in one packet" case shared by dmip_v4_fragment()
 */
static int emit_v4_whole_packet(const dmip_v4_header_t* header, const void* payload, size_t payload_len, dmip_v4_fragment_func_t callback, void* user_data)
{
    size_t total_len = DMIP_V4_HEADER_LEN + payload_len;
    uint8_t* packet = Dmod_Malloc(total_len);
    if (packet == NULL)
        return -ENOMEM;

    dmip_v4_header_t whole = *header;
    whole.total_length = (uint16_t)total_len;
    whole.flag_mf = false;
    whole.fragment_offset = 0;

    dmip_v4_build_header(packet, total_len, &whole);
    if (payload_len > 0)
        memcpy(packet + DMIP_V4_HEADER_LEN, payload, payload_len);

    callback(packet, total_len, user_data);
    Dmod_Free(packet);
    return 0;
}

/**
 * @brief Implementation of dmip_v4_fragment() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, int, _v4_fragment, ( const dmip_v4_header_t* header, const void* payload, size_t payload_len, uint16_t mtu, dmip_v4_fragment_func_t callback, void* user_data ))
{
    if (header == NULL || callback == NULL || (payload == NULL && payload_len > 0))
        return -EINVAL;
    if (header->src.family != dmip_family_v4 || header->dst.family != dmip_family_v4)
        return -EINVAL;
    if (mtu < DMIP_V4_HEADER_LEN + 8u)
        return -EINVAL;

    size_t total_len = DMIP_V4_HEADER_LEN + payload_len;
    if (total_len <= mtu)
        return emit_v4_whole_packet(header, payload, payload_len, callback, user_data);

    if (header->flag_df)
        return -EMSGSIZE;

    size_t max_chunk = ((size_t)mtu - DMIP_V4_HEADER_LEN) & ~((size_t)7u);
    uint8_t* fragment = Dmod_Malloc(mtu);
    if (fragment == NULL)
        return -ENOMEM;

    const uint8_t* src = (const uint8_t*)payload;
    size_t offset = 0;
    while (offset < payload_len)
    {
        size_t chunk_len = payload_len - offset;
        if (chunk_len > max_chunk)
            chunk_len = max_chunk;
        bool is_last = (offset + chunk_len) >= payload_len;

        dmip_v4_header_t frag_header = *header;
        frag_header.total_length = (uint16_t)(DMIP_V4_HEADER_LEN + chunk_len);
        frag_header.flag_mf = !is_last;
        frag_header.fragment_offset = (uint16_t)(offset / 8u);

        dmip_v4_build_header(fragment, mtu, &frag_header);
        memcpy(fragment + DMIP_V4_HEADER_LEN, src + offset, chunk_len);
        callback(fragment, DMIP_V4_HEADER_LEN + chunk_len, user_data);

        offset += chunk_len;
    }

    Dmod_Free(fragment);
    return 0;
}

/* ============================================================================
 *                      IPv6
 * ========================================================================== */

/**
 * @brief Implementation of dmip_v6_build_header() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, int, _v6_build_header, ( uint8_t* buffer, size_t buffer_len, const dmip_v6_header_t* header ))
{
    if (buffer == NULL || header == NULL || buffer_len < DMIP_V6_HEADER_LEN)
        return -EINVAL;
    if (header->src.family != dmip_family_v6 || header->dst.family != dmip_family_v6)
        return -EINVAL;

    buffer[0] = (uint8_t)((6u << 4) | (header->traffic_class >> 4));
    buffer[1] = (uint8_t)((header->traffic_class << 4) | ((header->flow_label >> 16) & 0x0Fu));
    buffer[2] = (uint8_t)((header->flow_label >> 8) & 0xFFu);
    buffer[3] = (uint8_t)(header->flow_label & 0xFFu);
    write_u16_be(&buffer[4], header->payload_length);
    buffer[6] = header->next_header;
    buffer[7] = header->hop_limit;
    memcpy(&buffer[8], header->src.addr.v6, DMIP_IPV6_ADDR_LEN);
    memcpy(&buffer[24], header->dst.addr.v6, DMIP_IPV6_ADDR_LEN);

    return 0;
}

/**
 * @brief Implementation of dmip_v6_parse_header() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, int, _v6_parse_header, ( const uint8_t* buffer, size_t length, dmip_v6_header_t* header ))
{
    if (buffer == NULL || header == NULL || length < DMIP_V6_HEADER_LEN)
        return -EINVAL;

    if ((buffer[0] >> 4) != 6u)
        return -EPROTO;

    header->traffic_class = (uint8_t)(((buffer[0] & 0x0Fu) << 4) | (buffer[1] >> 4));
    header->flow_label = (((uint32_t)buffer[1] & 0x0Fu) << 16) | ((uint32_t)buffer[2] << 8) | (uint32_t)buffer[3];
    header->payload_length = read_u16_be(&buffer[4]);
    header->next_header = buffer[6];
    header->hop_limit = buffer[7];
    header->src.family = dmip_family_v6;
    memcpy(header->src.addr.v6, &buffer[8], DMIP_IPV6_ADDR_LEN);
    header->dst.family = dmip_family_v6;
    memcpy(header->dst.addr.v6, &buffer[24], DMIP_IPV6_ADDR_LEN);

    if ((size_t)DMIP_V6_HEADER_LEN + header->payload_length > length)
        return -EPROTO;

    return 0;
}

/**
 * @brief Implementation of dmip_v6_decrement_hop_limit() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, int, _v6_decrement_hop_limit, ( uint8_t* buffer, size_t length ))
{
    if (buffer == NULL || length < DMIP_V6_HEADER_LEN)
        return -EINVAL;

    if (buffer[7] == 0)
        return -ETIMEDOUT;

    buffer[7] -= 1;
    return (buffer[7] == 0) ? -ETIMEDOUT : 0;
}

/**
 * @brief Implementation of dmip_v6_next_identification() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, uint32_t, _v6_next_identification, ( void ))
{
    dmosi_mutex_lock(g_id_mutex);
    uint32_t id = ++g_v6_next_id;
    dmosi_mutex_unlock(g_id_mutex);
    return id;
}

/**
 * @brief Emit a single unfragmented IPv6 packet - the "payload already
 *        fits in one packet" case shared by dmip_v6_fragment()
 */
static int emit_v6_whole_packet(const dmip_v6_header_t* header, const void* payload, size_t payload_len, dmip_v6_fragment_func_t callback, void* user_data)
{
    size_t total_len = DMIP_V6_HEADER_LEN + payload_len;
    uint8_t* packet = Dmod_Malloc(total_len);
    if (packet == NULL)
        return -ENOMEM;

    dmip_v6_header_t whole = *header;
    whole.payload_length = (uint16_t)payload_len;

    dmip_v6_build_header(packet, total_len, &whole);
    if (payload_len > 0)
        memcpy(packet + DMIP_V6_HEADER_LEN, payload, payload_len);

    callback(packet, total_len, user_data);
    Dmod_Free(packet);
    return 0;
}

/**
 * @brief Implementation of dmip_v6_fragment() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, int, _v6_fragment, ( const dmip_v6_header_t* header, const void* payload, size_t payload_len, uint16_t mtu, uint32_t identification, dmip_v6_fragment_func_t callback, void* user_data ))
{
    if (header == NULL || callback == NULL || (payload == NULL && payload_len > 0))
        return -EINVAL;
    if (header->src.family != dmip_family_v6 || header->dst.family != dmip_family_v6)
        return -EINVAL;

    size_t total_len = DMIP_V6_HEADER_LEN + payload_len;
    if (total_len <= mtu)
        return emit_v6_whole_packet(header, payload, payload_len, callback, user_data);

    size_t overhead = DMIP_V6_HEADER_LEN + DMIP_V6_FRAGMENT_HEADER_LEN;
    if (mtu <= overhead)
        return -EINVAL;

    size_t max_chunk = ((size_t)mtu - overhead) & ~((size_t)7u);
    if (max_chunk == 0)
        return -EINVAL;

    uint8_t* fragment = Dmod_Malloc(mtu);
    if (fragment == NULL)
        return -ENOMEM;

    const uint8_t* src = (const uint8_t*)payload;
    size_t offset = 0;
    while (offset < payload_len)
    {
        size_t chunk_len = payload_len - offset;
        if (chunk_len > max_chunk)
            chunk_len = max_chunk;
        bool is_last = (offset + chunk_len) >= payload_len;

        dmip_v6_header_t base = *header;
        base.next_header = DMIP_PROTO_IPV6_FRAGMENT;
        base.payload_length = (uint16_t)(DMIP_V6_FRAGMENT_HEADER_LEN + chunk_len);
        dmip_v6_build_header(fragment, mtu, &base);

        uint8_t* frag_hdr = fragment + DMIP_V6_HEADER_LEN;
        frag_hdr[0] = header->next_header;
        frag_hdr[1] = 0;
        write_u16_be(&frag_hdr[2], (uint16_t)(((offset / 8u) << 3) | (is_last ? 0u : 1u)));
        write_u32_be(&frag_hdr[4], identification);

        memcpy(fragment + overhead, src + offset, chunk_len);
        callback(fragment, overhead + chunk_len, user_data);

        offset += chunk_len;
    }

    Dmod_Free(fragment);
    return 0;
}

/* ============================================================================
 *                      Reassembly
 *
 * One system-wide table (g_reassembly, guarded by g_reassembly_mutex)
 * holds every in-progress reassembly, IPv4 and IPv6 both - each entry is
 * keyed by family plus a packed byte key (protocol/next_header,
 * identification, source and destination address, zero-padded to a
 * common DMIP_REASSEMBLY_KEY_LEN so both families' keys compare the same
 * way). The chunk list, coverage check and expiry logic are entirely
 * shared between families; only key construction and the final
 * packet-rebuild step differ (see finish_v4_reassembly()/
 * finish_v6_reassembly()).
 * ========================================================================== */

#define DMIP_REASSEMBLY_KEY_LEN 37u

struct dmip_chunk
{
    size_t   offset;
    size_t   length;
    uint8_t* data;
};

struct dmip_reassembly_entry
{
    dmip_family_t family;
    uint8_t       key[DMIP_REASSEMBLY_KEY_LEN];
    union
    {
        dmip_v4_header_t v4;
        dmip_v6_header_t v6;
    } header;
    dmlist_context_t* chunks;      /**< struct dmip_chunk* */
    bool               total_length_known;
    size_t             total_length;
    uint32_t           last_seen;  /**< dmosi_get_tick_count() at the last fragment accepted */
};

static void build_v4_key(uint8_t* key, uint8_t protocol, uint16_t identification, const dmip_addr_t* src, const dmip_addr_t* dst)
{
    memset(key, 0, DMIP_REASSEMBLY_KEY_LEN);
    key[0] = protocol;
    write_u16_be(&key[1], identification);
    memcpy(&key[3], src->addr.v4, DMIP_IPV4_ADDR_LEN);
    memcpy(&key[7], dst->addr.v4, DMIP_IPV4_ADDR_LEN);
}

static void build_v6_key(uint8_t* key, uint8_t next_header, uint32_t identification, const dmip_addr_t* src, const dmip_addr_t* dst)
{
    memset(key, 0, DMIP_REASSEMBLY_KEY_LEN);
    key[0] = next_header;
    write_u32_be(&key[1], identification);
    memcpy(&key[5], src->addr.v6, DMIP_IPV6_ADDR_LEN);
    memcpy(&key[21], dst->addr.v6, DMIP_IPV6_ADDR_LEN);
}

typedef struct
{
    dmip_family_t  family;
    const uint8_t* key;
} reassembly_needle_t;

static int compare_entry_key(const void* data, const void* user_data)
{
    const struct dmip_reassembly_entry* entry = (const struct dmip_reassembly_entry*)data;
    const reassembly_needle_t* needle = (const reassembly_needle_t*)user_data;

    if (entry->family != needle->family)
        return -1;

    for (size_t i = 0; i < DMIP_REASSEMBLY_KEY_LEN; i++)
    {
        if (entry->key[i] != needle->key[i])
            return -1;
    }
    return 0;
}

static int compare_pointer(const void* data, const void* user_data)
{
    return (data == user_data) ? 0 : -1;
}

static bool insert_chunk(dmlist_context_t* chunks, size_t offset, const uint8_t* data, size_t length)
{
    struct dmip_chunk* chunk = Dmod_Malloc(sizeof(*chunk));
    if (chunk == NULL)
        return false;

    chunk->data = Dmod_Malloc(length);
    if (chunk->data == NULL)
    {
        Dmod_Free(chunk);
        return false;
    }

    memcpy(chunk->data, data, length);
    chunk->offset = offset;
    chunk->length = length;

    if (!dmlist_push_back(chunks, chunk))
    {
        Dmod_Free(chunk->data);
        Dmod_Free(chunk);
        return false;
    }
    return true;
}

static int compare_chunk_offset(const void* data1, const void* data2)
{
    const struct dmip_chunk* a = (const struct dmip_chunk*)data1;
    const struct dmip_chunk* b = (const struct dmip_chunk*)data2;

    if (a->offset < b->offset)
        return -1;
    if (a->offset > b->offset)
        return 1;
    return 0;
}

typedef struct
{
    size_t covered;
    bool   contiguous;
} coverage_state_t;

static bool coverage_visit(void* data, void* user_data)
{
    struct dmip_chunk* chunk = (struct dmip_chunk*)data;
    coverage_state_t* state = (coverage_state_t*)user_data;

    if (chunk->offset > state->covered)
    {
        state->contiguous = false;
        return false;
    }

    size_t end = chunk->offset + chunk->length;
    if (end > state->covered)
        state->covered = end;

    return true;
}

/**
 * @brief Whether `chunks` (sorted in place by offset) cover every byte of
 *        [0, total_length) with no gaps
 */
static bool coverage_is_complete(dmlist_context_t* chunks, size_t total_length)
{
    dmlist_sort(chunks, compare_chunk_offset);

    coverage_state_t state = { .covered = 0, .contiguous = true };
    dmlist_foreach(chunks, coverage_visit, &state);

    return state.contiguous && state.covered >= total_length;
}

static bool copy_chunk_visit(void* data, void* user_data)
{
    struct dmip_chunk* chunk = (struct dmip_chunk*)data;
    uint8_t* dest = (uint8_t*)user_data;
    memcpy(dest + chunk->offset, chunk->data, chunk->length);
    return true;
}

static void copy_chunks_into(dmlist_context_t* chunks, uint8_t* dest)
{
    dmlist_foreach(chunks, copy_chunk_visit, dest);
}

/**
 * @brief Free every chunk in `entry`, its chunk list, then `entry` itself
 */
static void free_entry(struct dmip_reassembly_entry* entry)
{
    if (entry == NULL)
        return;

    size_t count = dmlist_size(entry->chunks);
    for (size_t i = 0; i < count; i++)
    {
        struct dmip_chunk* chunk = (struct dmip_chunk*)dmlist_pop_front(entry->chunks);
        Dmod_Free(chunk->data);
        Dmod_Free(chunk);
    }
    dmlist_destroy(entry->chunks);
    Dmod_Free(entry);
}

/**
 * @brief Find an existing reassembly entry matching (family, key), or
 *        create and register a fresh empty one - caller must hold
 *        g_reassembly_mutex
 */
static struct dmip_reassembly_entry* find_or_create_entry(dmip_family_t family, const uint8_t* key)
{
    reassembly_needle_t needle = { .family = family, .key = key };
    struct dmip_reassembly_entry* entry = (struct dmip_reassembly_entry*)dmlist_find(g_reassembly, &needle, compare_entry_key);
    if (entry != NULL)
        return entry;

    entry = Dmod_Malloc(sizeof(*entry));
    if (entry == NULL)
        return NULL;

    entry->family = family;
    memcpy(entry->key, key, DMIP_REASSEMBLY_KEY_LEN);
    entry->chunks = dmlist_create(Dmod_GetCurrentAllocatorName());
    entry->total_length_known = false;
    entry->total_length = 0;
    entry->last_seen = dmosi_get_tick_count();

    if (entry->chunks == NULL || !dmlist_push_back(g_reassembly, entry))
    {
        dmlist_destroy(entry->chunks);
        Dmod_Free(entry);
        return NULL;
    }
    return entry;
}

static bool entry_is_expired(const struct dmip_reassembly_entry* entry)
{
    return (uint32_t)(dmosi_get_tick_count() - entry->last_seen) >= DMIP_REASSEMBLY_TIMEOUT_MS;
}

/**
 * @brief Drop and free every reassembly entry that has been incomplete
 *        for longer than DMIP_REASSEMBLY_TIMEOUT_MS - caller must hold
 *        g_reassembly_mutex
 */
static void expire_old_entries(void)
{
    size_t i = 0;
    while (i < dmlist_size(g_reassembly))
    {
        struct dmip_reassembly_entry* entry = (struct dmip_reassembly_entry*)dmlist_get(g_reassembly, i);
        if (entry != NULL && entry_is_expired(entry))
        {
            dmlist_remove_at(g_reassembly, i);
            free_entry(entry);
        }
        else
        {
            i++;
        }
    }
}

/**
 * @brief Rebuild the complete IPv4 packet for a finished reassembly
 */
static int finish_v4_reassembly(struct dmip_reassembly_entry* entry, uint8_t** out_packet, size_t* out_length)
{
    size_t total = DMIP_V4_HEADER_LEN + entry->total_length;
    uint8_t* out = Dmod_Malloc(total);
    if (out == NULL)
        return -ENOMEM;

    dmip_v4_header_t final_header = entry->header.v4;
    final_header.total_length = (uint16_t)total;
    final_header.flag_mf = false;
    final_header.fragment_offset = 0;

    dmip_v4_build_header(out, total, &final_header);
    copy_chunks_into(entry->chunks, out + DMIP_V4_HEADER_LEN);

    *out_packet = out;
    *out_length = total;
    return 0;
}

/**
 * @brief Implementation of dmip_v4_reassemble() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, int, _v4_reassemble, ( const uint8_t* fragment, size_t length, uint8_t** out_packet, size_t* out_length ))
{
    if (fragment == NULL || out_packet == NULL || out_length == NULL)
        return -EINVAL;

    dmip_v4_header_t header;
    size_t header_len = 0;
    int result = dmip_v4_parse_header(fragment, length, &header, &header_len);
    if (result != 0)
        return result;

    size_t payload_len = header.total_length - header_len;
    if (!header.flag_mf && header.fragment_offset == 0)
    {
        uint8_t* copy = Dmod_Malloc(header.total_length);
        if (copy == NULL)
            return -ENOMEM;
        memcpy(copy, fragment, header.total_length);
        *out_packet = copy;
        *out_length = header.total_length;
        return 0;
    }

    uint8_t key[DMIP_REASSEMBLY_KEY_LEN];
    build_v4_key(key, header.protocol, header.identification, &header.src, &header.dst);

    dmosi_mutex_lock(g_reassembly_mutex);
    expire_old_entries();

    struct dmip_reassembly_entry* entry = find_or_create_entry(dmip_family_v4, key);
    if (entry == NULL)
    {
        dmosi_mutex_unlock(g_reassembly_mutex);
        return -ENOMEM;
    }

    if (dmlist_is_empty(entry->chunks))
        entry->header.v4 = header;

    size_t frag_offset = (size_t)header.fragment_offset * 8u;
    if (!insert_chunk(entry->chunks, frag_offset, fragment + header_len, payload_len))
    {
        dmosi_mutex_unlock(g_reassembly_mutex);
        return -ENOMEM;
    }

    if (!header.flag_mf)
    {
        entry->total_length = frag_offset + payload_len;
        entry->total_length_known = true;
    }
    entry->last_seen = dmosi_get_tick_count();

    int status = -EINPROGRESS;
    if (entry->total_length_known && coverage_is_complete(entry->chunks, entry->total_length))
    {
        status = finish_v4_reassembly(entry, out_packet, out_length);
        dmlist_remove(g_reassembly, entry, compare_pointer);
        free_entry(entry);
    }

    dmosi_mutex_unlock(g_reassembly_mutex);
    return status;
}

/**
 * @brief Rebuild the complete IPv6 packet for a finished reassembly
 */
static int finish_v6_reassembly(struct dmip_reassembly_entry* entry, uint8_t** out_packet, size_t* out_length)
{
    size_t total = DMIP_V6_HEADER_LEN + entry->total_length;
    uint8_t* out = Dmod_Malloc(total);
    if (out == NULL)
        return -ENOMEM;

    dmip_v6_header_t final_header = entry->header.v6;
    final_header.payload_length = (uint16_t)entry->total_length;

    dmip_v6_build_header(out, total, &final_header);
    copy_chunks_into(entry->chunks, out + DMIP_V6_HEADER_LEN);

    *out_packet = out;
    *out_length = total;
    return 0;
}

struct v6_frag_info
{
    uint8_t  original_next_header;
    uint32_t identification;
    size_t   offset;
    bool     more_fragments;
};

/**
 * @brief Parse the IPv6 Fragment extension header immediately following
 *        the 40-byte fixed header
 */
static int parse_v6_fragment_header(const uint8_t* fragment, const dmip_v6_header_t* header, struct v6_frag_info* info)
{
    if (header->payload_length < DMIP_V6_FRAGMENT_HEADER_LEN)
        return -EPROTO;

    const uint8_t* frag_hdr = fragment + DMIP_V6_HEADER_LEN;
    uint16_t offset_and_flags = read_u16_be(&frag_hdr[2]);

    info->original_next_header = frag_hdr[0];
    info->identification = read_u32_be(&frag_hdr[4]);
    info->offset = (size_t)(offset_and_flags >> 3) * 8u;
    info->more_fragments = (offset_and_flags & 0x0001u) != 0;
    return 0;
}

/**
 * @brief Implementation of dmip_v6_reassemble() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, int, _v6_reassemble, ( const uint8_t* fragment, size_t length, uint8_t** out_packet, size_t* out_length ))
{
    if (fragment == NULL || out_packet == NULL || out_length == NULL)
        return -EINVAL;

    dmip_v6_header_t header;
    int result = dmip_v6_parse_header(fragment, length, &header);
    if (result != 0)
        return result;

    if (header.next_header != DMIP_PROTO_IPV6_FRAGMENT)
    {
        size_t total = DMIP_V6_HEADER_LEN + header.payload_length;
        uint8_t* copy = Dmod_Malloc(total);
        if (copy == NULL)
            return -ENOMEM;
        memcpy(copy, fragment, total);
        *out_packet = copy;
        *out_length = total;
        return 0;
    }

    struct v6_frag_info info;
    result = parse_v6_fragment_header(fragment, &header, &info);
    if (result != 0)
        return result;

    header.next_header = info.original_next_header;
    size_t payload_len = header.payload_length - DMIP_V6_FRAGMENT_HEADER_LEN;
    const uint8_t* payload = fragment + DMIP_V6_HEADER_LEN + DMIP_V6_FRAGMENT_HEADER_LEN;

    uint8_t key[DMIP_REASSEMBLY_KEY_LEN];
    build_v6_key(key, header.next_header, info.identification, &header.src, &header.dst);

    dmosi_mutex_lock(g_reassembly_mutex);
    expire_old_entries();

    struct dmip_reassembly_entry* entry = find_or_create_entry(dmip_family_v6, key);
    if (entry == NULL)
    {
        dmosi_mutex_unlock(g_reassembly_mutex);
        return -ENOMEM;
    }

    if (dmlist_is_empty(entry->chunks))
        entry->header.v6 = header;

    if (!insert_chunk(entry->chunks, info.offset, payload, payload_len))
    {
        dmosi_mutex_unlock(g_reassembly_mutex);
        return -ENOMEM;
    }

    if (!info.more_fragments)
    {
        entry->total_length = info.offset + payload_len;
        entry->total_length_known = true;
    }
    entry->last_seen = dmosi_get_tick_count();

    int status = -EINPROGRESS;
    if (entry->total_length_known && coverage_is_complete(entry->chunks, entry->total_length))
    {
        status = finish_v6_reassembly(entry, out_packet, out_length);
        dmlist_remove(g_reassembly, entry, compare_pointer);
        free_entry(entry);
    }

    dmosi_mutex_unlock(g_reassembly_mutex);
    return status;
}

/* ============================================================================
 *                      Send / Receive
 *
 * Send still reaches outside dmip, but only as far as dmnetbridge now
 * (routing/ARP/frame I/O all moved there - see this file's top comment).
 * Receive is push-based via dmnetbridge's packet_received DIF - see
 * "Receive queue" further below.
 * ========================================================================== */

#define DMIP_ETH_HEADER_LEN     14u
#define DMIP_ETHERTYPE_IPV4     0x0800u
#define DMIP_ETHERTYPE_IPV6     0x86DDu

/**
 * @brief Implementation of dmip_v4_get_source_address() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, int, _v4_get_source_address, ( const dmip_addr_t* dst, dmip_addr_t* out_src ))
{
    if (dst == NULL || out_src == NULL || dst->family != dmip_family_v4)
        return -EINVAL;

    return dmnetbridge_get_source_address(dst, out_src);
}

/**
 * @brief dmip_v4_fragment_func_t state for dmip_v4_send() - transmits
 *        each emitted IP fragment via dmnetbridge_send()
 */
typedef struct
{
    const dmip_addr_t* dst;
    uint32_t            arp_timeout_ms;
    int                 result;    /**< First failure seen so far, or 0 */
} send_v4_ctx_t;

/**
 * @brief dmip_v4_fragment_func_t implementation backing dmip_v4_send()
 *
 * Once ctx->result records a failure, later calls are skipped rather than
 * attempting to send after something has already gone wrong - there is no
 * way to signal dmip_v4_fragment() to stop early (its callback returns
 * void), so this just makes every call after the first failure a no-op.
 *
 * Calls dmnetbridge_send() once per fragment rather than resolving the
 * route/MAC once up front and reusing it for every fragment - a multi-
 * fragment packet re-does that lookup once per fragment (cheap: an
 * already-cached dmroute/dmarp lookup, not a fresh ARP round trip, unless
 * the very first fragment's own resolution is still in flight). Accepted
 * in exchange for dmip having zero knowledge of routing/interfaces/ARP -
 * see this file's top comment.
 */
static void send_v4_fragment(const uint8_t* fragment, size_t fragment_len, void* user_data)
{
    send_v4_ctx_t* ctx = (send_v4_ctx_t*)user_data;
    if (ctx->result != 0)
        return;

    ctx->result = dmnetbridge_send(ctx->dst, DMIP_ETHERTYPE_IPV4, fragment, fragment_len, ctx->arp_timeout_ms, NULL);
}

/**
 * @brief Implementation of dmip_v4_send() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, int, _v4_send, ( const dmip_v4_header_t* header, const void* payload, size_t payload_len, uint32_t arp_timeout_ms ))
{
    if (header == NULL || (payload == NULL && payload_len > 0) || header->dst.family != dmip_family_v4)
        return -EINVAL;

    dmip_v4_header_t full_header = *header;
    if (full_header.src.family == dmip_family_none)
    {
        dmnetbridge_get_source_address(&header->dst, &full_header.src); /* best-effort, same as dmnetbridge_send()'s own resolution */
    }

    uint16_t mtu = DMNETIF_DEFAULT_MTU;
    dmnetbridge_get_mtu(&header->dst, &mtu); /* best-effort - mtu keeps the default above on failure */

    send_v4_ctx_t ctx = { .dst = &header->dst, .arp_timeout_ms = arp_timeout_ms, .result = 0 };

    int result = dmip_v4_fragment(&full_header, payload, payload_len, mtu, send_v4_fragment, &ctx);
    if (result != 0)
        return result;

    return ctx.result;
}

/**
 * @brief Implementation of dmip_send() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, int, _send, ( const dmip_header_t* header, const void* payload, size_t payload_len, uint32_t arp_timeout_ms ))
{
    if (header == NULL)
        return -EINVAL;

    switch (header->family)
    {
        case dmip_family_v4:
            return dmip_v4_send(&header->header.v4, payload, payload_len, arp_timeout_ms);
        case dmip_family_v6:
            /* No dmip_v6_send() yet - see the comment above
             * dmip_v6_reassemble()'s declaration in dmip.h for why (no
             * NDP module to resolve a next-hop MAC through). */
            return -ENOSYS;
        default:
            return -EINVAL;
    }
}

/* ============================================================================
 *                      Protocol dispatch
 *
 * dmip_dmnetbridge_packet_received() (this module's implementation of
 * dmnetbridge's packet_received DIF) is called by whichever thread is
 * pumping a given interface (dmnetbridge_handle_netif_rx(), run by
 * networkd), for every frame that turns out to be a completed IPv4/IPv6
 * packet. Rather than queuing it for whoever next calls a generic
 * receive function - which is what let a UDP consumer's call
 * accidentally steal (and then discard, per dmudp_receive()'s own
 * protocol check) a packet actually meant for some other protocol -
 * dmip looks at the packet's protocol number (IPv4's `protocol` field,
 * IPv6's `next_header` - the same numbering space, DMIP_PROTO_* in
 * dmip.h) and calls exactly the handler registered for it
 * (dmip_register_protocol()), falling back to the registered default
 * handler (dmip_register_default_protocol()) if none matches, or
 * dropping the packet if neither exists.
 * ========================================================================== */

/**
 * @brief dmlist_compare_func_t matching a struct dmip_protocol_entry
 *        against a `const uint8_t*` protocol number needle
 */
static int compare_protocol(const void* data, const void* user_data)
{
    const struct dmip_protocol_entry* entry = (const struct dmip_protocol_entry*)data;
    uint8_t protocol = *(const uint8_t*)user_data;
    return (entry->protocol == protocol) ? 0 : -1;
}

/**
 * @brief Implementation of dmip_register_protocol() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, int, _register_protocol, ( uint8_t protocol, dmip_protocol_handler_t handler ))
{
    if (handler == NULL)
        return -EINVAL;

    dmosi_mutex_lock(g_protocol_mutex);

    int result;
    if (dmlist_find(g_protocol_handlers, &protocol, compare_protocol) != NULL)
    {
        result = -EEXIST;
    }
    else
    {
        struct dmip_protocol_entry* entry = Dmod_Malloc(sizeof(*entry));
        if (entry == NULL)
        {
            result = -ENOMEM;
        }
        else
        {
            entry->protocol = protocol;
            entry->handler = handler;
            if (dmlist_push_back(g_protocol_handlers, entry))
            {
                result = 0;
            }
            else
            {
                Dmod_Free(entry);
                result = -ENOMEM;
            }
        }
    }

    dmosi_mutex_unlock(g_protocol_mutex);
    return result;
}

/**
 * @brief Implementation of dmip_unregister_protocol() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, void, _unregister_protocol, ( uint8_t protocol ))
{
    dmosi_mutex_lock(g_protocol_mutex);

    struct dmip_protocol_entry* entry = (struct dmip_protocol_entry*)dmlist_find(g_protocol_handlers, &protocol, compare_protocol);
    if (entry != NULL)
    {
        dmlist_remove(g_protocol_handlers, entry, compare_pointer);
        Dmod_Free(entry);
    }

    dmosi_mutex_unlock(g_protocol_mutex);
}

/**
 * @brief Implementation of dmip_register_default_protocol() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, int, _register_default_protocol, ( dmip_protocol_handler_t handler ))
{
    if (handler == NULL)
        return -EINVAL;

    dmosi_mutex_lock(g_protocol_mutex);

    int result = (g_default_handler != NULL) ? -EEXIST : 0;
    if (result == 0)
    {
        g_default_handler = handler;
    }

    dmosi_mutex_unlock(g_protocol_mutex);
    return result;
}

/**
 * @brief Implementation of dmip_unregister_default_protocol() - see dmip.h
 */
dmod_dmip_api_declaration(1.0, void, _unregister_default_protocol, ( void ))
{
    dmosi_mutex_lock(g_protocol_mutex);
    g_default_handler = NULL;
    dmosi_mutex_unlock(g_protocol_mutex);
}

/**
 * @brief Look up the registered (or default) handler for `protocol`,
 *        call it with `packet` if one exists, then free `packet`
 *
 * Copies the matched handler out from under g_protocol_mutex before
 * calling it, rather than holding the lock into another module's code -
 * a handler is free to call dmip_register_protocol()/_unregister_protocol()
 * itself (e.g. from its own dmod_init()/_deinit()) without deadlocking.
 */
static void dispatch_packet(uint8_t protocol, dmip_family_t family, dmnetif_iface_t iface, uint8_t* packet, size_t length)
{
    dmosi_mutex_lock(g_protocol_mutex);
    struct dmip_protocol_entry* entry = (struct dmip_protocol_entry*)dmlist_find(g_protocol_handlers, &protocol, compare_protocol);
    dmip_protocol_handler_t handler = (entry != NULL) ? entry->handler : g_default_handler;
    dmosi_mutex_unlock(g_protocol_mutex);

    if (handler != NULL)
    {
        handler(family, iface, packet, length);
    }

    Dmod_Free(packet);
}

/**
 * @brief Implementation of dmip's packet_received DIF - see dmnetbridge.h
 *
 * Checks the ethertype, strips the 14-byte L2 header, and feeds the rest
 * through dmip_v4_reassemble()/_v6_reassemble() unchanged. A completed
 * packet's header is parsed once more (dmip_v4_parse_header()/
 * _v6_parse_header()) purely to read its protocol number - reassembly
 * itself already parses internally but doesn't expose the parsed struct
 * - then dispatch_packet() takes it from there. Anything else (a
 * different ethertype, a still-incomplete reassembly, or a malformed/
 * rejected packet) is silently dropped, same as before.
 */
dmod_dmnetbridge_dif_api_declaration(1.0, dmip, void, _packet_received, ( dmnetif_iface_t iface, const uint8_t* frame, size_t frame_len ))
{
    if (frame == NULL || frame_len < DMIP_ETH_HEADER_LEN)
        return;

    uint16_t ethertype = read_u16_be(&frame[12]);

    dmip_family_t family;
    int result;
    uint8_t* packet = NULL;
    size_t length = 0;

    if (ethertype == DMIP_ETHERTYPE_IPV4)
    {
        family = dmip_family_v4;
        result = dmip_v4_reassemble(frame + DMIP_ETH_HEADER_LEN, frame_len - DMIP_ETH_HEADER_LEN, &packet, &length);
    }
    else if (ethertype == DMIP_ETHERTYPE_IPV6)
    {
        family = dmip_family_v6;
        result = dmip_v6_reassemble(frame + DMIP_ETH_HEADER_LEN, frame_len - DMIP_ETH_HEADER_LEN, &packet, &length);
    }
    else
    {
        return;
    }

    if (result != 0)
        return; /* -EINPROGRESS (still reassembling) or a malformed/rejected fragment - nothing to dispatch either way */

    uint8_t protocol;
    if (family == dmip_family_v4)
    {
        dmip_v4_header_t header = { 0 };
        size_t header_len = 0;
        if (dmip_v4_parse_header(packet, length, &header, &header_len) != 0)
        {
            Dmod_Free(packet);
            return;
        }
        protocol = header.protocol;
    }
    else
    {
        dmip_v6_header_t header = { 0 };
        if (dmip_v6_parse_header(packet, length, &header) != 0)
        {
            Dmod_Free(packet);
            return;
        }
        protocol = header.next_header;
    }

    dispatch_packet(protocol, family, iface, packet, length);
}

/* ---- DMOD lifecycle ---- */

/**
 * @brief Module initialization - allocates the reassembly table, the
 *        protocol dispatch table, and their guarding mutexes
 */
int dmod_init(const Dmod_Config_t *Config)
{
    (void)Config;

    g_reassembly = dmlist_create(Dmod_GetCurrentAllocatorName());
    g_reassembly_mutex = dmosi_mutex_create(false);
    g_id_mutex = dmosi_mutex_create(false);
    g_protocol_handlers = dmlist_create(Dmod_GetCurrentAllocatorName());
    g_protocol_mutex = dmosi_mutex_create(false);
    if (
        g_reassembly == NULL || g_reassembly_mutex == NULL || g_id_mutex == NULL
     || g_protocol_handlers == NULL || g_protocol_mutex == NULL
        )
    {
        DMOD_LOG_ERROR("Failed to allocate dmip state\n");
        return -1;
    }

    g_v4_next_id = 0;
    g_v6_next_id = 0;
    g_default_handler = NULL;

    DMOD_LOG_INFO("DMIP initialized\n");
    return 0;
}

/**
 * @brief Module deinitialization - frees every in-progress reassembly and
 *        every protocol registration, then the tables and mutexes
 */
int dmod_deinit(void)
{
    size_t count = dmlist_size(g_reassembly);
    for (size_t i = 0; i < count; i++)
    {
        free_entry((struct dmip_reassembly_entry*)dmlist_pop_front(g_reassembly));
    }
    dmlist_destroy(g_reassembly);
    g_reassembly = NULL;

    dmosi_mutex_destroy(g_reassembly_mutex);
    g_reassembly_mutex = NULL;
    dmosi_mutex_destroy(g_id_mutex);
    g_id_mutex = NULL;

    size_t protocol_count = dmlist_size(g_protocol_handlers);
    for (size_t i = 0; i < protocol_count; i++)
    {
        Dmod_Free(dmlist_pop_front(g_protocol_handlers));
    }
    dmlist_destroy(g_protocol_handlers);
    g_protocol_handlers = NULL;

    dmosi_mutex_destroy(g_protocol_mutex);
    g_protocol_mutex = NULL;
    g_default_handler = NULL;

    DMOD_LOG_INFO("DMIP deinitialized\n");
    return 0;
}
