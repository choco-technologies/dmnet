/**
 * @file dmip_test.c
 * @brief Test steps for dmip
 *
 * Covers the address type (family enum / union shape - relied on by
 * dmnetif's and dmroute's own field-by-field conversions), the RFC 1071
 * checksum primitive, IPv4/IPv6 header build/parse round trips, TTL/
 * Hop-Limit decrement, identification counters, and fragmentation/
 * reassembly for both families (including out-of-order fragment
 * delivery and the "already whole" passthrough path).
 *
 * No dmod_test_setup()/_teardown() fixture is needed: fragmentation
 * tests draw a fresh identification from dmip_v4_next_identification()/
 * dmip_v6_next_identification() (monotonic, so never collides with an
 * earlier test's in-progress reassembly), and every other test either
 * doesn't touch the reassembly table at all (build/parse, TTL, the
 * single-packet fragment/passthrough cases) or completes it within the
 * same step.
 */
#include "dmod_test.h"
#include "dmip.h"
#include <string.h>
#include <errno.h>

static dmip_addr_t make_v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    dmip_addr_t ip = { 0 };
    ip.family = dmip_family_v4;
    ip.addr.v4[0] = a;
    ip.addr.v4[1] = b;
    ip.addr.v4[2] = c;
    ip.addr.v4[3] = d;
    return ip;
}

static dmip_addr_t make_v6(uint8_t last_byte)
{
    dmip_addr_t ip = { 0 };
    ip.family = dmip_family_v6;
    ip.addr.v6[0] = 0x20;
    ip.addr.v6[1] = 0x01;
    ip.addr.v6[DMIP_IPV6_ADDR_LEN - 1] = last_byte;
    return ip;
}

/* dmod modules have no libc memcmp() (see dmod/src/module/string.c's
 * minimal replacement set) - a small manual comparison stands in for it. */
static bool bytes_equal(const uint8_t* a, const uint8_t* b, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        if (a[i] != b[i])
            return false;
    }
    return true;
}

#define MAX_TEST_FRAGMENTS     8
#define MAX_TEST_FRAGMENT_LEN  128

/**
 * @brief Collects every fragment dmip_v4_fragment()/dmip_v6_fragment()
 *        emits, copying each one out (the buffer they hand the callback
 *        is only valid for the duration of the call)
 */
typedef struct
{
    uint8_t data[MAX_TEST_FRAGMENTS][MAX_TEST_FRAGMENT_LEN];
    size_t  length[MAX_TEST_FRAGMENTS];
    size_t  count;
} fragment_collector_t;

static void collect_fragment(const uint8_t* fragment, size_t fragment_len, void* user_data)
{
    fragment_collector_t* collector = (fragment_collector_t*)user_data;
    memcpy(collector->data[collector->count], fragment, fragment_len);
    collector->length[collector->count] = fragment_len;
    collector->count++;
}

/* ---- Address type ---- */

DMOD_TEST_STEP(family_enum_values_are_distinct)
{
    DMOD_TEST_EXPECT_NE(dmip_family_none, dmip_family_v4);
    DMOD_TEST_EXPECT_NE(dmip_family_none, dmip_family_v6);
    DMOD_TEST_EXPECT_NE(dmip_family_v4, dmip_family_v6);
}

DMOD_TEST_STEP(addr_union_fits_both_families)
{
    dmip_addr_t addr = { 0 };
    DMOD_TEST_EXPECT_EQ(sizeof(addr.addr.v4), (size_t)DMIP_IPV4_ADDR_LEN);
    DMOD_TEST_EXPECT_EQ(sizeof(addr.addr.v6), (size_t)DMIP_IPV6_ADDR_LEN);
}

DMOD_TEST_STEP(v4_and_v6_share_leading_storage)
{
    /* dmnetif.c/dmroute.c's mask/compare helpers read/write through
     * addr.v6[i] for i < DMIP_IPV4_ADDR_LEN regardless of which member
     * was last assigned - only valid because v4 and v6 are the leading
     * bytes of the same union storage. */
    dmip_addr_t addr = { 0 };
    addr.addr.v4[0] = 0xAB;
    DMOD_TEST_EXPECT_EQ(addr.addr.v6[0], (uint8_t)0xAB);
}

/* ---- Checksum ---- */

DMOD_TEST_STEP(checksum_of_empty_buffer_is_all_ones)
{
    uint8_t dummy = 0;
    DMOD_TEST_EXPECT_EQ(dmip_checksum(&dummy, 0), (uint16_t)0xFFFF);
}

DMOD_TEST_STEP(checksum_self_verifies_when_embedded)
{
    uint8_t data[6] = { 0x45, 0x00, 0x00, 0x1C, 0x00, 0x00 };

    data[4] = 0;
    data[5] = 0;
    uint16_t checksum = dmip_checksum(data, sizeof(data));
    data[4] = (uint8_t)(checksum >> 8);
    data[5] = (uint8_t)(checksum & 0xFF);

    DMOD_TEST_EXPECT_EQ(dmip_checksum(data, sizeof(data)), (uint16_t)0);
}

/* ---- IPv4: build / parse ---- */

DMOD_TEST_STEP(v4_build_then_parse_roundtrip)
{
    dmip_v4_header_t header = { 0 };
    header.dscp = 10;
    header.ecn = 1;
    header.total_length = DMIP_V4_HEADER_LEN;
    header.identification = 0x1234;
    header.flag_df = true;
    header.flag_mf = false;
    header.ttl = 64;
    header.protocol = DMIP_PROTO_UDP;
    header.src = make_v4(192, 168, 1, 10);
    header.dst = make_v4(192, 168, 1, 20);

    uint8_t buffer[DMIP_V4_HEADER_LEN];
    DMOD_TEST_EXPECT_EQ(dmip_v4_build_header(buffer, sizeof(buffer), &header), 0);
    DMOD_TEST_EXPECT_TRUE(dmip_v4_checksum_valid(buffer, sizeof(buffer)));

    dmip_v4_header_t parsed = { 0 };
    size_t header_len = 0;
    DMOD_TEST_EXPECT_EQ(dmip_v4_parse_header(buffer, sizeof(buffer), &parsed, &header_len), 0);
    DMOD_TEST_EXPECT_EQ(header_len, (size_t)DMIP_V4_HEADER_LEN);
    DMOD_TEST_EXPECT_EQ(parsed.dscp, header.dscp);
    DMOD_TEST_EXPECT_EQ(parsed.ecn, header.ecn);
    DMOD_TEST_EXPECT_EQ(parsed.total_length, header.total_length);
    DMOD_TEST_EXPECT_EQ(parsed.identification, header.identification);
    DMOD_TEST_EXPECT_TRUE(parsed.flag_df);
    DMOD_TEST_EXPECT_FALSE(parsed.flag_mf);
    DMOD_TEST_EXPECT_EQ(parsed.fragment_offset, header.fragment_offset);
    DMOD_TEST_EXPECT_EQ(parsed.ttl, header.ttl);
    DMOD_TEST_EXPECT_EQ(parsed.protocol, header.protocol);
    DMOD_TEST_EXPECT_TRUE(bytes_equal(parsed.src.addr.v4, header.src.addr.v4, DMIP_IPV4_ADDR_LEN));
    DMOD_TEST_EXPECT_TRUE(bytes_equal(parsed.dst.addr.v4, header.dst.addr.v4, DMIP_IPV4_ADDR_LEN));
}

DMOD_TEST_STEP(v4_checksum_invalid_after_corruption)
{
    dmip_v4_header_t header = { 0 };
    header.total_length = DMIP_V4_HEADER_LEN;
    header.ttl = 32;
    header.protocol = DMIP_PROTO_TCP;
    header.src = make_v4(10, 1, 1, 1);
    header.dst = make_v4(10, 1, 1, 2);

    uint8_t buffer[DMIP_V4_HEADER_LEN];
    dmip_v4_build_header(buffer, sizeof(buffer), &header);
    DMOD_TEST_EXPECT_TRUE(dmip_v4_checksum_valid(buffer, sizeof(buffer)));

    buffer[9] ^= 0xFF; /* corrupt the protocol byte */
    DMOD_TEST_EXPECT_FALSE(dmip_v4_checksum_valid(buffer, sizeof(buffer)));
}

DMOD_TEST_STEP(v4_build_rejects_bad_arguments)
{
    dmip_v4_header_t header = { 0 };
    header.src = make_v4(1, 1, 1, 1);
    header.dst = make_v4(1, 1, 1, 2);

    uint8_t small[DMIP_V4_HEADER_LEN - 1];
    DMOD_TEST_EXPECT_EQ(dmip_v4_build_header(small, sizeof(small), &header), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmip_v4_build_header(NULL, DMIP_V4_HEADER_LEN, &header), -EINVAL);

    dmip_v4_header_t bad_family = header;
    bad_family.src.family = dmip_family_v6;
    uint8_t buffer[DMIP_V4_HEADER_LEN];
    DMOD_TEST_EXPECT_EQ(dmip_v4_build_header(buffer, sizeof(buffer), &bad_family), -EINVAL);
}

DMOD_TEST_STEP(v4_parse_rejects_short_buffer_and_bad_version)
{
    dmip_v4_header_t parsed = { 0 };
    size_t header_len = 0;

    uint8_t short_buf[DMIP_V4_HEADER_LEN - 1] = { 0 };
    DMOD_TEST_EXPECT_EQ(dmip_v4_parse_header(short_buf, sizeof(short_buf), &parsed, &header_len), -EINVAL);

    uint8_t buffer[DMIP_V4_HEADER_LEN] = { 0 };
    buffer[0] = (6u << 4) | 5u; /* wrong version */
    DMOD_TEST_EXPECT_EQ(dmip_v4_parse_header(buffer, sizeof(buffer), &parsed, &header_len), -EPROTO);
}

DMOD_TEST_STEP(v4_parse_honors_larger_ihl_and_skips_options)
{
    uint8_t buffer[24] = { 0 };
    buffer[0] = (4u << 4) | 6u; /* version 4, IHL 6 -> 24-byte header */
    buffer[3] = 24;             /* total_length = 24 (big-endian, low byte) */
    buffer[8] = 64;             /* ttl */
    buffer[9] = DMIP_PROTO_TCP;
    buffer[12] = 10;
    buffer[16] = 10;
    buffer[19] = 2;

    dmip_v4_header_t parsed = { 0 };
    size_t header_len = 0;
    DMOD_TEST_EXPECT_EQ(dmip_v4_parse_header(buffer, sizeof(buffer), &parsed, &header_len), 0);
    DMOD_TEST_EXPECT_EQ(header_len, (size_t)24);
    DMOD_TEST_EXPECT_EQ(parsed.protocol, (uint8_t)DMIP_PROTO_TCP);
}

/* ---- IPv4: TTL ---- */

DMOD_TEST_STEP(v4_decrement_ttl_reduces_by_one_and_fixes_checksum)
{
    dmip_v4_header_t header = { 0 };
    header.total_length = DMIP_V4_HEADER_LEN;
    header.ttl = 10;
    header.protocol = DMIP_PROTO_UDP;
    header.src = make_v4(10, 0, 0, 1);
    header.dst = make_v4(10, 0, 0, 2);

    uint8_t buffer[DMIP_V4_HEADER_LEN];
    dmip_v4_build_header(buffer, sizeof(buffer), &header);

    DMOD_TEST_EXPECT_EQ(dmip_v4_decrement_ttl(buffer, sizeof(buffer)), 0);
    DMOD_TEST_EXPECT_EQ(buffer[8], (uint8_t)9);
    DMOD_TEST_EXPECT_TRUE(dmip_v4_checksum_valid(buffer, sizeof(buffer)));
}

DMOD_TEST_STEP(v4_decrement_ttl_reaching_zero_returns_etimedout)
{
    dmip_v4_header_t header = { 0 };
    header.total_length = DMIP_V4_HEADER_LEN;
    header.ttl = 1;
    header.protocol = DMIP_PROTO_UDP;
    header.src = make_v4(10, 0, 0, 1);
    header.dst = make_v4(10, 0, 0, 2);

    uint8_t buffer[DMIP_V4_HEADER_LEN];
    dmip_v4_build_header(buffer, sizeof(buffer), &header);

    DMOD_TEST_EXPECT_EQ(dmip_v4_decrement_ttl(buffer, sizeof(buffer)), -ETIMEDOUT);
    DMOD_TEST_EXPECT_EQ(buffer[8], (uint8_t)0);
}

DMOD_TEST_STEP(v4_decrement_ttl_already_zero_returns_etimedout)
{
    dmip_v4_header_t header = { 0 };
    header.total_length = DMIP_V4_HEADER_LEN;
    header.ttl = 0;
    header.protocol = DMIP_PROTO_UDP;
    header.src = make_v4(10, 0, 0, 1);
    header.dst = make_v4(10, 0, 0, 2);

    uint8_t buffer[DMIP_V4_HEADER_LEN];
    dmip_v4_build_header(buffer, sizeof(buffer), &header);

    DMOD_TEST_EXPECT_EQ(dmip_v4_decrement_ttl(buffer, sizeof(buffer)), -ETIMEDOUT);
    DMOD_TEST_EXPECT_EQ(buffer[8], (uint8_t)0);
}

/* ---- IPv4: identification ---- */

DMOD_TEST_STEP(v4_next_identification_increments)
{
    uint16_t first = dmip_v4_next_identification();
    uint16_t second = dmip_v4_next_identification();
    DMOD_TEST_EXPECT_EQ(second, (uint16_t)(first + 1));
}

/* ---- IPv4: fragmentation / reassembly ---- */

DMOD_TEST_STEP(v4_fragment_small_payload_emits_one_unfragmented_packet)
{
    uint8_t payload[10];
    for (size_t i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(50 + i);

    dmip_v4_header_t header = { 0 };
    header.ttl = DMIP_DEFAULT_TTL;
    header.protocol = DMIP_PROTO_UDP;
    header.identification = dmip_v4_next_identification();
    header.src = make_v4(10, 0, 0, 7);
    header.dst = make_v4(10, 0, 0, 8);

    fragment_collector_t collector = { .count = 0 };
    DMOD_TEST_EXPECT_EQ(dmip_v4_fragment(&header, payload, sizeof(payload), 1500, collect_fragment, &collector), 0);
    DMOD_TEST_EXPECT_EQ(collector.count, (size_t)1);

    dmip_v4_header_t parsed = { 0 };
    size_t header_len = 0;
    DMOD_TEST_EXPECT_EQ(dmip_v4_parse_header(collector.data[0], collector.length[0], &parsed, &header_len), 0);
    DMOD_TEST_EXPECT_FALSE(parsed.flag_mf);
    DMOD_TEST_EXPECT_EQ(parsed.fragment_offset, (uint16_t)0);
    DMOD_TEST_EXPECT_EQ(parsed.total_length, (uint16_t)(DMIP_V4_HEADER_LEN + sizeof(payload)));
    DMOD_TEST_EXPECT_TRUE(bytes_equal(collector.data[0] + header_len, payload, sizeof(payload)));
}

DMOD_TEST_STEP(v4_fragment_df_flag_set_and_too_large_returns_emsgsize)
{
    uint8_t payload[64] = { 0 };
    dmip_v4_header_t header = { 0 };
    header.ttl = DMIP_DEFAULT_TTL;
    header.protocol = DMIP_PROTO_UDP;
    header.flag_df = true;
    header.identification = dmip_v4_next_identification();
    header.src = make_v4(10, 0, 0, 9);
    header.dst = make_v4(10, 0, 0, 10);

    fragment_collector_t collector = { .count = 0 };
    DMOD_TEST_EXPECT_EQ(dmip_v4_fragment(&header, payload, sizeof(payload), 40, collect_fragment, &collector), -EMSGSIZE);
    DMOD_TEST_EXPECT_EQ(collector.count, (size_t)0);
}

DMOD_TEST_STEP(v4_fragment_large_payload_splits_and_reassembles_in_order)
{
    uint8_t payload[30];
    for (size_t i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)i;

    dmip_v4_header_t header = { 0 };
    header.ttl = DMIP_DEFAULT_TTL;
    header.protocol = DMIP_PROTO_UDP;
    header.identification = dmip_v4_next_identification();
    header.src = make_v4(10, 0, 1, 1);
    header.dst = make_v4(10, 0, 1, 2);

    fragment_collector_t collector = { .count = 0 };
    DMOD_TEST_EXPECT_EQ(dmip_v4_fragment(&header, payload, sizeof(payload), 28, collect_fragment, &collector), 0);
    DMOD_TEST_EXPECT_EQ(collector.count, (size_t)4);

    uint8_t* reassembled = NULL;
    size_t reassembled_len = 0;
    int result = -EINPROGRESS;
    for (size_t i = 0; i < collector.count; i++)
    {
        result = dmip_v4_reassemble(collector.data[i], collector.length[i], &reassembled, &reassembled_len);
        if (i + 1 < collector.count)
            DMOD_TEST_EXPECT_EQ(result, -EINPROGRESS);
    }

    DMOD_TEST_EXPECT_EQ(result, 0);
    DMOD_TEST_EXPECT_EQ(reassembled_len, (size_t)(DMIP_V4_HEADER_LEN + sizeof(payload)));
    DMOD_TEST_EXPECT_TRUE(bytes_equal(reassembled + DMIP_V4_HEADER_LEN, payload, sizeof(payload)));
    DMOD_TEST_EXPECT_TRUE(dmip_v4_checksum_valid(reassembled, DMIP_V4_HEADER_LEN));

    Dmod_Free(reassembled);
}

DMOD_TEST_STEP(v4_reassemble_accepts_out_of_order_fragments)
{
    uint8_t payload[24];
    for (size_t i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(100 + i);

    dmip_v4_header_t header = { 0 };
    header.ttl = DMIP_DEFAULT_TTL;
    header.protocol = DMIP_PROTO_UDP;
    header.identification = dmip_v4_next_identification();
    header.src = make_v4(10, 0, 2, 1);
    header.dst = make_v4(10, 0, 2, 2);

    fragment_collector_t collector = { .count = 0 };
    DMOD_TEST_EXPECT_EQ(dmip_v4_fragment(&header, payload, sizeof(payload), 28, collect_fragment, &collector), 0);
    DMOD_TEST_EXPECT_EQ(collector.count, (size_t)3);

    uint8_t* reassembled = NULL;
    size_t reassembled_len = 0;
    int result = -EINPROGRESS;
    for (size_t i = collector.count; i > 0; i--)
    {
        result = dmip_v4_reassemble(collector.data[i - 1], collector.length[i - 1], &reassembled, &reassembled_len);
    }

    DMOD_TEST_EXPECT_EQ(result, 0);
    DMOD_TEST_EXPECT_TRUE(bytes_equal(reassembled + DMIP_V4_HEADER_LEN, payload, sizeof(payload)));

    Dmod_Free(reassembled);
}

DMOD_TEST_STEP(v4_reassemble_passes_through_unfragmented_packet)
{
    uint8_t payload[5] = { 1, 2, 3, 4, 5 };
    dmip_v4_header_t header = { 0 };
    header.total_length = DMIP_V4_HEADER_LEN + sizeof(payload);
    header.ttl = DMIP_DEFAULT_TTL;
    header.protocol = DMIP_PROTO_UDP;
    header.src = make_v4(10, 0, 3, 1);
    header.dst = make_v4(10, 0, 3, 2);

    uint8_t packet[DMIP_V4_HEADER_LEN + sizeof(payload)];
    dmip_v4_build_header(packet, sizeof(packet), &header);
    memcpy(packet + DMIP_V4_HEADER_LEN, payload, sizeof(payload));

    uint8_t* out = NULL;
    size_t out_len = 0;
    DMOD_TEST_EXPECT_EQ(dmip_v4_reassemble(packet, sizeof(packet), &out, &out_len), 0);
    DMOD_TEST_EXPECT_EQ(out_len, sizeof(packet));
    DMOD_TEST_EXPECT_TRUE(bytes_equal(out, packet, sizeof(packet)));

    Dmod_Free(out);
}

DMOD_TEST_STEP(v4_reassemble_invalid_packet_returns_error)
{
    uint8_t garbage[4] = { 0 };
    uint8_t* out = NULL;
    size_t out_len = 0;
    DMOD_TEST_EXPECT_EQ(dmip_v4_reassemble(garbage, sizeof(garbage), &out, &out_len), -EINVAL);
}

/* ---- IPv6: build / parse ---- */

DMOD_TEST_STEP(v6_build_then_parse_roundtrip)
{
    dmip_v6_header_t header = { 0 };
    header.traffic_class = 0x2C;
    header.flow_label = 0x0ABCDE;
    header.payload_length = 0;
    header.next_header = DMIP_PROTO_TCP;
    header.hop_limit = 55;
    header.src = make_v6(1);
    header.dst = make_v6(2);

    uint8_t buffer[DMIP_V6_HEADER_LEN];
    DMOD_TEST_EXPECT_EQ(dmip_v6_build_header(buffer, sizeof(buffer), &header), 0);

    dmip_v6_header_t parsed = { 0 };
    DMOD_TEST_EXPECT_EQ(dmip_v6_parse_header(buffer, sizeof(buffer), &parsed), 0);
    DMOD_TEST_EXPECT_EQ(parsed.traffic_class, header.traffic_class);
    DMOD_TEST_EXPECT_EQ(parsed.flow_label, header.flow_label);
    DMOD_TEST_EXPECT_EQ(parsed.payload_length, header.payload_length);
    DMOD_TEST_EXPECT_EQ(parsed.next_header, header.next_header);
    DMOD_TEST_EXPECT_EQ(parsed.hop_limit, header.hop_limit);
    DMOD_TEST_EXPECT_TRUE(bytes_equal(parsed.src.addr.v6, header.src.addr.v6, DMIP_IPV6_ADDR_LEN));
    DMOD_TEST_EXPECT_TRUE(bytes_equal(parsed.dst.addr.v6, header.dst.addr.v6, DMIP_IPV6_ADDR_LEN));
}

DMOD_TEST_STEP(v6_build_rejects_bad_arguments)
{
    dmip_v6_header_t header = { 0 };
    header.src = make_v6(1);
    header.dst = make_v6(2);

    uint8_t small[DMIP_V6_HEADER_LEN - 1];
    DMOD_TEST_EXPECT_EQ(dmip_v6_build_header(small, sizeof(small), &header), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmip_v6_build_header(NULL, DMIP_V6_HEADER_LEN, &header), -EINVAL);

    dmip_v6_header_t bad_family = header;
    bad_family.src.family = dmip_family_v4;
    uint8_t buffer[DMIP_V6_HEADER_LEN];
    DMOD_TEST_EXPECT_EQ(dmip_v6_build_header(buffer, sizeof(buffer), &bad_family), -EINVAL);
}

DMOD_TEST_STEP(v6_parse_rejects_short_buffer_and_bad_version)
{
    dmip_v6_header_t parsed = { 0 };
    uint8_t short_buf[DMIP_V6_HEADER_LEN - 1] = { 0 };
    DMOD_TEST_EXPECT_EQ(dmip_v6_parse_header(short_buf, sizeof(short_buf), &parsed), -EINVAL);

    uint8_t buffer[DMIP_V6_HEADER_LEN] = { 0 };
    buffer[0] = (4u << 4); /* wrong version */
    DMOD_TEST_EXPECT_EQ(dmip_v6_parse_header(buffer, sizeof(buffer), &parsed), -EPROTO);
}

/* ---- IPv6: Hop Limit ---- */

DMOD_TEST_STEP(v6_decrement_hop_limit_reduces_by_one)
{
    dmip_v6_header_t header = { 0 };
    header.hop_limit = 10;
    header.next_header = DMIP_PROTO_UDP;
    header.src = make_v6(3);
    header.dst = make_v6(4);

    uint8_t buffer[DMIP_V6_HEADER_LEN];
    dmip_v6_build_header(buffer, sizeof(buffer), &header);

    DMOD_TEST_EXPECT_EQ(dmip_v6_decrement_hop_limit(buffer, sizeof(buffer)), 0);
    DMOD_TEST_EXPECT_EQ(buffer[7], (uint8_t)9);
}

DMOD_TEST_STEP(v6_decrement_hop_limit_reaching_zero_returns_etimedout)
{
    dmip_v6_header_t header = { 0 };
    header.hop_limit = 1;
    header.next_header = DMIP_PROTO_UDP;
    header.src = make_v6(5);
    header.dst = make_v6(6);

    uint8_t buffer[DMIP_V6_HEADER_LEN];
    dmip_v6_build_header(buffer, sizeof(buffer), &header);

    DMOD_TEST_EXPECT_EQ(dmip_v6_decrement_hop_limit(buffer, sizeof(buffer)), -ETIMEDOUT);
    DMOD_TEST_EXPECT_EQ(buffer[7], (uint8_t)0);
}

DMOD_TEST_STEP(v6_decrement_hop_limit_already_zero_returns_etimedout)
{
    dmip_v6_header_t header = { 0 };
    header.hop_limit = 0;
    header.next_header = DMIP_PROTO_UDP;
    header.src = make_v6(7);
    header.dst = make_v6(8);

    uint8_t buffer[DMIP_V6_HEADER_LEN];
    dmip_v6_build_header(buffer, sizeof(buffer), &header);

    DMOD_TEST_EXPECT_EQ(dmip_v6_decrement_hop_limit(buffer, sizeof(buffer)), -ETIMEDOUT);
    DMOD_TEST_EXPECT_EQ(buffer[7], (uint8_t)0);
}

/* ---- IPv6: identification ---- */

DMOD_TEST_STEP(v6_next_identification_increments)
{
    uint32_t first = dmip_v6_next_identification();
    uint32_t second = dmip_v6_next_identification();
    DMOD_TEST_EXPECT_EQ(second, first + 1);
}

/* ---- IPv6: fragmentation / reassembly ---- */

DMOD_TEST_STEP(v6_fragment_small_payload_emits_one_unfragmented_packet)
{
    uint8_t payload[10];
    for (size_t i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(150 + i);

    dmip_v6_header_t header = { 0 };
    header.hop_limit = DMIP_DEFAULT_HOP_LIMIT;
    header.next_header = DMIP_PROTO_UDP;
    header.src = make_v6(13);
    header.dst = make_v6(14);

    fragment_collector_t collector = { .count = 0 };
    DMOD_TEST_EXPECT_EQ(dmip_v6_fragment(&header, payload, sizeof(payload), 1500, 0, collect_fragment, &collector), 0);
    DMOD_TEST_EXPECT_EQ(collector.count, (size_t)1);

    dmip_v6_header_t parsed = { 0 };
    DMOD_TEST_EXPECT_EQ(dmip_v6_parse_header(collector.data[0], collector.length[0], &parsed), 0);
    DMOD_TEST_EXPECT_EQ(parsed.next_header, (uint8_t)DMIP_PROTO_UDP);
    DMOD_TEST_EXPECT_EQ(parsed.payload_length, (uint16_t)sizeof(payload));
    DMOD_TEST_EXPECT_TRUE(bytes_equal(collector.data[0] + DMIP_V6_HEADER_LEN, payload, sizeof(payload)));
}

DMOD_TEST_STEP(v6_fragment_large_payload_splits_and_reassembles_in_order)
{
    uint8_t payload[30];
    for (size_t i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(200 + i);

    dmip_v6_header_t header = { 0 };
    header.hop_limit = DMIP_DEFAULT_HOP_LIMIT;
    header.next_header = DMIP_PROTO_UDP;
    header.src = make_v6(11);
    header.dst = make_v6(12);

    uint32_t identification = dmip_v6_next_identification();

    fragment_collector_t collector = { .count = 0 };
    DMOD_TEST_EXPECT_EQ(dmip_v6_fragment(&header, payload, sizeof(payload), 56, identification, collect_fragment, &collector), 0);
    DMOD_TEST_EXPECT_EQ(collector.count, (size_t)4);

    uint8_t* reassembled = NULL;
    size_t reassembled_len = 0;
    int result = -EINPROGRESS;
    for (size_t i = 0; i < collector.count; i++)
    {
        result = dmip_v6_reassemble(collector.data[i], collector.length[i], &reassembled, &reassembled_len);
        if (i + 1 < collector.count)
            DMOD_TEST_EXPECT_EQ(result, -EINPROGRESS);
    }

    DMOD_TEST_EXPECT_EQ(result, 0);
    DMOD_TEST_EXPECT_EQ(reassembled_len, (size_t)(DMIP_V6_HEADER_LEN + sizeof(payload)));
    DMOD_TEST_EXPECT_TRUE(bytes_equal(reassembled + DMIP_V6_HEADER_LEN, payload, sizeof(payload)));

    dmip_v6_header_t parsed = { 0 };
    DMOD_TEST_EXPECT_EQ(dmip_v6_parse_header(reassembled, reassembled_len, &parsed), 0);
    DMOD_TEST_EXPECT_EQ(parsed.next_header, (uint8_t)DMIP_PROTO_UDP);

    Dmod_Free(reassembled);
}

DMOD_TEST_STEP(v6_reassemble_accepts_out_of_order_fragments)
{
    uint8_t payload[24];
    for (size_t i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(50 + i);

    dmip_v6_header_t header = { 0 };
    header.hop_limit = DMIP_DEFAULT_HOP_LIMIT;
    header.next_header = DMIP_PROTO_UDP;
    header.src = make_v6(21);
    header.dst = make_v6(22);

    uint32_t identification = dmip_v6_next_identification();

    fragment_collector_t collector = { .count = 0 };
    DMOD_TEST_EXPECT_EQ(dmip_v6_fragment(&header, payload, sizeof(payload), 56, identification, collect_fragment, &collector), 0);
    DMOD_TEST_EXPECT_EQ(collector.count, (size_t)3);

    uint8_t* reassembled = NULL;
    size_t reassembled_len = 0;
    int result = -EINPROGRESS;
    for (size_t i = collector.count; i > 0; i--)
    {
        result = dmip_v6_reassemble(collector.data[i - 1], collector.length[i - 1], &reassembled, &reassembled_len);
    }

    DMOD_TEST_EXPECT_EQ(result, 0);
    DMOD_TEST_EXPECT_TRUE(bytes_equal(reassembled + DMIP_V6_HEADER_LEN, payload, sizeof(payload)));

    Dmod_Free(reassembled);
}

DMOD_TEST_STEP(v6_reassemble_passes_through_unfragmented_packet)
{
    uint8_t payload[6] = { 9, 8, 7, 6, 5, 4 };
    dmip_v6_header_t header = { 0 };
    header.payload_length = sizeof(payload);
    header.hop_limit = DMIP_DEFAULT_HOP_LIMIT;
    header.next_header = DMIP_PROTO_UDP;
    header.src = make_v6(15);
    header.dst = make_v6(16);

    uint8_t packet[DMIP_V6_HEADER_LEN + sizeof(payload)];
    dmip_v6_build_header(packet, sizeof(packet), &header);
    memcpy(packet + DMIP_V6_HEADER_LEN, payload, sizeof(payload));

    uint8_t* out = NULL;
    size_t out_len = 0;
    DMOD_TEST_EXPECT_EQ(dmip_v6_reassemble(packet, sizeof(packet), &out, &out_len), 0);
    DMOD_TEST_EXPECT_EQ(out_len, sizeof(packet));
    DMOD_TEST_EXPECT_TRUE(bytes_equal(out, packet, sizeof(packet)));

    Dmod_Free(out);
}

DMOD_TEST_STEP(v6_reassemble_invalid_packet_returns_error)
{
    uint8_t garbage[4] = { 0 };
    uint8_t* out = NULL;
    size_t out_len = 0;
    DMOD_TEST_EXPECT_EQ(dmip_v6_reassemble(garbage, sizeof(garbage), &out, &out_len), -EINVAL);
}
