/**
 * @file dmip_test.c
 * @brief Test steps for dmip
 *
 * Covers the address type (family enum / union shape - relied on by
 * dmnetif's and dmroute's own field-by-field conversions), the RFC 1071
 * checksum primitive, IPv4/IPv6 header build/parse round trips, TTL/
 * Hop-Limit decrement, identification counters, fragmentation/reassembly
 * for both families (including out-of-order fragment delivery and the
 * "already whole" passthrough path), and send/receive.
 *
 * The send/receive steps register two "/null"-backed dmnetif fixture
 * interfaces (same pattern as lib/dmarp/tests/dmarp_test.c) via
 * dmod_test_setup()/_teardown(), wrapping every step in this file
 * regardless of whether that particular step uses them. No real driver
 * backs either fixture, so neither can ever actually go "up" - a full
 * dmip_v4_send() transmission can't be exercised end-to-end here, but
 * everything up to the point a real driver would be needed can: route
 * lookup and ARP resolution (both now inside dmnetbridge_send() - a
 * cache-hit ARP entry is seeded by hand, the same "no real driver"
 * limitation dmarp_test.c documents for its own cache-miss path applies
 * here too), fragmentation, and Ethernet framing, failing only at the
 * final dmnetif_send() (-EIO, interface not up).
 *
 * Receiving is push-based (see dmip.c's "Receive queue" section) - there
 * is no interface to poll anymore, so the receive steps instead simulate
 * what dmnetbridge_handle_netif_rx() would do after a real
 * dmnetif_receive(): feed_packet() below builds a minimal Ethernet frame
 * by hand and drives it straight into dmip's packet_received DIF
 * implementation via Dmod_GetNextDifModule()/Dmod_GetDifFunction() - the
 * same discovery dmnetbridge_handle_netif_rx() itself uses, see
 * dmnetbridge.c's broadcast_packet_received(). This requires
 * ENABLE_DIF_REGISTRATIONS (see dmnetbridge.h's own doc comment on
 * dmod_dmnetbridge_packet_received_sig) and linking dmnetbridge_if.
 *
 * Every send/receive step uses a distinct destination network (never
 * reused across steps), since dmroute's routes and dmarp's cache are
 * both global state that outlives a single test step - same discipline
 * dmarp_test.c documents for its own cache entries.
 */
#define ENABLE_DIF_REGISTRATIONS ON
#include "dmod_test.h"
#include "dmip.h"
#include "dmroute.h"
#include "dmnetif.h"
#include "dmarp.h"
#include "dmnetbridge.h"
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

#define TEST_DEVICE_PATH_0 "/null"
#define TEST_DEVICE_PATH_1 "/null2"

static dmnetif_iface_t g_iface0 = NULL;
static dmnetif_iface_t g_iface1 = NULL;

void dmod_test_setup(void)
{
    g_iface0 = dmnetif_register("test0", TEST_DEVICE_PATH_0);
    g_iface1 = dmnetif_register("test1", TEST_DEVICE_PATH_1);
}

void dmod_test_teardown(void)
{
    dmnetif_unregister(g_iface0);
    dmnetif_unregister(g_iface1);
    g_iface0 = NULL;
    g_iface1 = NULL;
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

/* ---- IPv4: get_source_address ---- */

DMOD_TEST_STEP(v4_get_source_address_returns_egress_interface_ip)
{
    dmip_addr_t iface_ip = make_v4(10, 10, 0, 1);
    dmnetif_set_ip_address(g_iface0, &iface_ip);

    dmip_addr_t dest_net = make_v4(10, 10, 0, 0);
    dmip_addr_t netmask = make_v4(255, 255, 255, 0);
    dmroute_route_t route = dmroute_add(&dest_net, &netmask, NULL, "test0", DMROUTE_DEFAULT_METRIC, dmroute_origin_static);

    dmip_addr_t target = make_v4(10, 10, 0, 42);
    dmip_addr_t src = { 0 };
    DMOD_TEST_EXPECT_EQ(dmip_v4_get_source_address(&target, &src), 0);
    DMOD_TEST_EXPECT_EQ(src.family, dmip_family_v4);
    DMOD_TEST_EXPECT_TRUE(bytes_equal(src.addr.v4, iface_ip.addr.v4, DMIP_IPV4_ADDR_LEN));

    dmroute_remove(route);
    dmip_addr_t clear = { 0 };
    dmnetif_set_ip_address(g_iface0, &clear);
}

DMOD_TEST_STEP(v4_get_source_address_no_route_returns_enetunreach)
{
    dmip_addr_t target = make_v4(192, 0, 2, 200); /* TEST-NET-1 - no route added anywhere in this file */
    dmip_addr_t src = { 0 };
    DMOD_TEST_EXPECT_EQ(dmip_v4_get_source_address(&target, &src), -ENETUNREACH);
}

DMOD_TEST_STEP(v4_get_source_address_rejects_bad_arguments)
{
    dmip_addr_t target = make_v4(10, 0, 0, 1);
    dmip_addr_t src = { 0 };

    DMOD_TEST_EXPECT_EQ(dmip_v4_get_source_address(NULL, &src), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmip_v4_get_source_address(&target, NULL), -EINVAL);

    dmip_addr_t bad_family = target;
    bad_family.family = dmip_family_v6;
    DMOD_TEST_EXPECT_EQ(dmip_v4_get_source_address(&bad_family, &src), -EINVAL);
}

/* ---- IPv4: send ---- */

DMOD_TEST_STEP(v4_send_rejects_bad_arguments)
{
    dmip_v4_header_t header = { 0 };
    header.dst = make_v4(10, 0, 0, 1);

    DMOD_TEST_EXPECT_EQ(dmip_v4_send(NULL, NULL, 0, DMARP_DEFAULT_TIMEOUT_MS), -EINVAL);

    dmip_v4_header_t bad_family = header;
    bad_family.dst.family = dmip_family_v6;
    DMOD_TEST_EXPECT_EQ(dmip_v4_send(&bad_family, NULL, 0, DMARP_DEFAULT_TIMEOUT_MS), -EINVAL);
}

DMOD_TEST_STEP(v4_send_no_route_returns_enetunreach)
{
    dmip_v4_header_t header = { 0 };
    header.dst = make_v4(203, 0, 113, 1); /* TEST-NET-3 - no route added anywhere in this file */
    header.protocol = DMIP_PROTO_UDP;

    DMOD_TEST_EXPECT_EQ(dmip_v4_send(&header, NULL, 0, DMARP_DEFAULT_TIMEOUT_MS), -ENETUNREACH);
}

DMOD_TEST_STEP(v4_send_route_to_unregistered_iface_returns_enodev)
{
    dmip_addr_t dest_net = make_v4(198, 51, 100, 0);
    dmip_addr_t netmask = make_v4(255, 255, 255, 0);
    dmroute_route_t route = dmroute_add(&dest_net, &netmask, NULL, "does-not-exist", DMROUTE_DEFAULT_METRIC, dmroute_origin_static);

    dmip_v4_header_t header = { 0 };
    header.dst = make_v4(198, 51, 100, 5);
    header.protocol = DMIP_PROTO_UDP;

    DMOD_TEST_EXPECT_EQ(dmip_v4_send(&header, NULL, 0, DMARP_DEFAULT_TIMEOUT_MS), -ENODEV);

    dmroute_remove(route);
}

DMOD_TEST_STEP(v4_send_full_path_without_real_driver_returns_eio)
{
    /* Route + a hand-seeded ARP cache entry let dmip_v4_send() -> the
     * per-fragment dmnetbridge_send() call - run its entire pipeline
     * (route lookup, ARP cache hit, fragmentation, Ethernet framing)
     * without needing a real driver - it only fails at the very last
     * step, dmnetif_send() against an interface that could never be
     * brought up (no real driver behind "/null"). */
    dmip_addr_t dest_net = make_v4(172, 16, 5, 0);
    dmip_addr_t netmask = make_v4(255, 255, 255, 0);
    dmroute_route_t route = dmroute_add(&dest_net, &netmask, NULL, "test0", DMROUTE_DEFAULT_METRIC, dmroute_origin_static);

    dmip_addr_t dest = make_v4(172, 16, 5, 10);
    dmnetif_mac_addr_t fake_mac = { { 0x02, 0x00, 0x00, 0x00, 0x00, 0x10 } };
    dmarp_cache_insert(g_iface0, &dest, &fake_mac);

    dmip_v4_header_t header = { 0 };
    header.dst = dest;
    header.protocol = DMIP_PROTO_UDP;
    header.ttl = DMIP_DEFAULT_TTL;
    header.identification = dmip_v4_next_identification();

    uint8_t payload[4] = { 1, 2, 3, 4 };
    DMOD_TEST_EXPECT_EQ(dmip_v4_send(&header, payload, sizeof(payload), DMARP_DEFAULT_TIMEOUT_MS), -EIO);

    dmarp_cache_remove(g_iface0, &dest);
    dmroute_remove(route);
}

/* ---- Receive plumbing shared by protocol-dispatch tests below ----
 *
 * Receiving is push-based (see dmip.c's "Protocol dispatch" section) -
 * dmip implements dmnetbridge's packet_received DIF
 * (dmip_dmnetbridge_packet_received()) and, once a frame turns out to be
 * a completed IP packet, dispatches it to whichever module registered
 * for its protocol number. feed_packet() below calls that DIF
 * implementation's exact counterpart - a hand-built Ethernet frame - the
 * same way dmnetbridge_handle_netif_rx() would after actually reading one
 * off a real interface, without needing a real driver or a background
 * pump thread to exercise it.
 */

#define TEST_ETH_HEADER_LEN 14u
#define TEST_ETHERTYPE_IPV4 0x0800u
#define TEST_ETHERTYPE_IPV6 0x86DDu

/* IANA reserves 253/254 "for experimentation and testing" (RFC 3692) -
 * guaranteed not to collide with any real DMIP_PROTO_* this binary might
 * ever add a handler for. */
#define TEST_PROTOCOL_A 253u
#define TEST_PROTOCOL_B 254u

static void write_u16_be(uint8_t* p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)(value & 0xFF);
}

/**
 * @brief Build a minimal Ethernet frame around `packet` and broadcast it
 *        to every packet_received DIF implementor (dmip's own, in
 *        practice, since this binary doesn't link any other implementor) -
 *        the same discovery dmnetbridge_handle_netif_rx() itself uses
 *        after a real dmnetif_receive(), see
 *        dmnetbridge.c's broadcast_packet_received()
 */
static void feed_packet(dmnetif_iface_t iface, uint16_t ethertype, const uint8_t* packet, size_t packet_len)
{
    size_t frame_len = TEST_ETH_HEADER_LEN + packet_len;
    uint8_t* frame = Dmod_Malloc(frame_len);
    memset(frame, 0, TEST_ETH_HEADER_LEN);
    write_u16_be(&frame[12], ethertype);
    memcpy(frame + TEST_ETH_HEADER_LEN, packet, packet_len);

    Dmod_Context_t* implementor = NULL;
    while ((implementor = Dmod_GetNextDifModule(dmod_dmnetbridge_packet_received_sig, implementor)) != NULL)
    {
        dmod_dmnetbridge_packet_received_t fn =
            (dmod_dmnetbridge_packet_received_t)Dmod_GetDifFunction(implementor, dmod_dmnetbridge_packet_received_sig);
        if (fn != NULL)
        {
            fn(iface, frame, frame_len);
        }
    }

    Dmod_Free(frame);
}

/**
 * @brief Build a minimal, valid, unfragmented IPv4 packet (header + payload)
 */
static size_t build_v4_packet(uint8_t* buffer, size_t buffer_len, uint8_t protocol, dmip_addr_t src, dmip_addr_t dst, const uint8_t* payload, size_t payload_len)
{
    dmip_v4_header_t header = { 0 };
    header.total_length = (uint16_t)(DMIP_V4_HEADER_LEN + payload_len);
    header.ttl = DMIP_DEFAULT_TTL;
    header.protocol = protocol;
    header.src = src;
    header.dst = dst;

    dmip_v4_build_header(buffer, buffer_len, &header);
    memcpy(buffer + DMIP_V4_HEADER_LEN, payload, payload_len);
    return DMIP_V4_HEADER_LEN + payload_len;
}

/**
 * @brief Build a minimal, valid, unfragmented IPv6 packet (header + payload)
 */
static size_t build_v6_packet(uint8_t* buffer, size_t buffer_len, uint8_t next_header, dmip_addr_t src, dmip_addr_t dst, const uint8_t* payload, size_t payload_len)
{
    dmip_v6_header_t header = { 0 };
    header.payload_length = (uint16_t)payload_len;
    header.hop_limit = DMIP_DEFAULT_HOP_LIMIT;
    header.next_header = next_header;
    header.src = src;
    header.dst = dst;

    dmip_v6_build_header(buffer, buffer_len, &header);
    memcpy(buffer + DMIP_V6_HEADER_LEN, payload, payload_len);
    return DMIP_V6_HEADER_LEN + payload_len;
}

/* ---- Protocol dispatch ----
 *
 * dmip dispatches a completed packet to exactly the handler registered
 * for its protocol number (dmip_register_protocol()), a registered
 * default handler, or drops it - never to a shared queue any receive
 * call might pop from regardless of protocol (see dmip.md's "Protocol
 * dispatch" section for the bug this replaced). Every step below
 * registers, feeds a packet, asserts, then unregisters - state here
 * would otherwise leak across steps, same discipline dmarp_test.c
 * documents for its own cache entries.
 */

typedef struct
{
    bool            called;
    dmip_family_t   family;
    dmnetif_iface_t iface;
    uint8_t         packet[128];
    size_t          packet_len;
} handler_call_t;

static handler_call_t g_last_call;
static handler_call_t g_default_call;

static void record_call(dmip_family_t family, dmnetif_iface_t iface, const uint8_t* packet, size_t packet_len)
{
    g_last_call.called = true;
    g_last_call.family = family;
    g_last_call.iface = iface;
    g_last_call.packet_len = packet_len;
    memcpy(g_last_call.packet, packet, packet_len);
}

/**
 * @brief Distinct from record_call() so specific_registration_wins_over_default
 *        can tell whether the specific or the default registrant actually
 *        fired, rather than both writing the same g_last_call
 */
static void record_default_call(dmip_family_t family, dmnetif_iface_t iface, const uint8_t* packet, size_t packet_len)
{
    g_default_call.called = true;
    g_default_call.family = family;
    g_default_call.iface = iface;
    g_default_call.packet_len = packet_len;
    memcpy(g_default_call.packet, packet, packet_len);
}

DMOD_TEST_STEP(register_protocol_delivers_matching_v4_packet)
{
    memset(&g_last_call, 0, sizeof(g_last_call));
    DMOD_TEST_EXPECT_EQ(dmip_register_protocol(TEST_PROTOCOL_A, record_call), 0);

    uint8_t payload[4] = { 11, 22, 33, 44 };
    uint8_t packet[DMIP_V4_HEADER_LEN + sizeof(payload)];
    dmip_addr_t src = make_v4(10, 3, 0, 1);
    dmip_addr_t dst = make_v4(10, 3, 0, 2);
    size_t packet_len = build_v4_packet(packet, sizeof(packet), TEST_PROTOCOL_A, src, dst, payload, sizeof(payload));

    feed_packet(g_iface0, TEST_ETHERTYPE_IPV4, packet, packet_len);

    DMOD_TEST_EXPECT_TRUE(g_last_call.called);
    DMOD_TEST_EXPECT_EQ(g_last_call.family, dmip_family_v4);
    DMOD_TEST_EXPECT_EQ(g_last_call.iface, g_iface0);
    DMOD_TEST_EXPECT_EQ(g_last_call.packet_len, packet_len);
    DMOD_TEST_EXPECT_TRUE(bytes_equal(g_last_call.packet, packet, packet_len));

    dmip_unregister_protocol(TEST_PROTOCOL_A);
}

DMOD_TEST_STEP(register_protocol_delivers_matching_v6_packet)
{
    /* Same protocol number, the other family - proves DMIP_PROTO_* is one
     * shared numbering space, not per-family (IPv4's protocol field and
     * IPv6's next_header both feed the same dispatch table). */
    memset(&g_last_call, 0, sizeof(g_last_call));
    DMOD_TEST_EXPECT_EQ(dmip_register_protocol(TEST_PROTOCOL_A, record_call), 0);

    uint8_t payload[3] = { 7, 8, 9 };
    uint8_t packet[DMIP_V6_HEADER_LEN + sizeof(payload)];
    size_t packet_len = build_v6_packet(packet, sizeof(packet), TEST_PROTOCOL_A, make_v6(31), make_v6(32), payload, sizeof(payload));

    feed_packet(g_iface1, TEST_ETHERTYPE_IPV6, packet, packet_len);

    DMOD_TEST_EXPECT_TRUE(g_last_call.called);
    DMOD_TEST_EXPECT_EQ(g_last_call.family, dmip_family_v6);
    DMOD_TEST_EXPECT_EQ(g_last_call.iface, g_iface1);
    DMOD_TEST_EXPECT_EQ(g_last_call.packet_len, packet_len);
    DMOD_TEST_EXPECT_TRUE(bytes_equal(g_last_call.packet, packet, packet_len));

    dmip_unregister_protocol(TEST_PROTOCOL_A);
}

DMOD_TEST_STEP(register_protocol_ignores_non_matching_packet)
{
    /* A packet claimed by nobody (TEST_PROTOCOL_B, no registrant, no
     * default) must not reach a handler registered for a different
     * protocol (TEST_PROTOCOL_A) - the exact cross-protocol stealing bug
     * this whole mechanism replaced (see dmip.md). */
    memset(&g_last_call, 0, sizeof(g_last_call));
    DMOD_TEST_EXPECT_EQ(dmip_register_protocol(TEST_PROTOCOL_A, record_call), 0);

    uint8_t payload[2] = { 1, 2 };
    uint8_t packet[DMIP_V4_HEADER_LEN + sizeof(payload)];
    size_t packet_len = build_v4_packet(packet, sizeof(packet), TEST_PROTOCOL_B, make_v4(10, 3, 1, 1), make_v4(10, 3, 1, 2), payload, sizeof(payload));

    feed_packet(g_iface0, TEST_ETHERTYPE_IPV4, packet, packet_len);

    DMOD_TEST_EXPECT_FALSE(g_last_call.called);

    dmip_unregister_protocol(TEST_PROTOCOL_A);
}

DMOD_TEST_STEP(register_protocol_rejects_null_handler)
{
    DMOD_TEST_EXPECT_EQ(dmip_register_protocol(TEST_PROTOCOL_A, NULL), -EINVAL);
}

DMOD_TEST_STEP(register_protocol_twice_returns_eexist)
{
    DMOD_TEST_EXPECT_EQ(dmip_register_protocol(TEST_PROTOCOL_A, record_call), 0);
    DMOD_TEST_EXPECT_EQ(dmip_register_protocol(TEST_PROTOCOL_A, record_call), -EEXIST);

    dmip_unregister_protocol(TEST_PROTOCOL_A);
}

DMOD_TEST_STEP(unregister_protocol_stops_delivery)
{
    memset(&g_last_call, 0, sizeof(g_last_call));
    DMOD_TEST_EXPECT_EQ(dmip_register_protocol(TEST_PROTOCOL_A, record_call), 0);
    dmip_unregister_protocol(TEST_PROTOCOL_A);

    uint8_t payload[2] = { 3, 4 };
    uint8_t packet[DMIP_V4_HEADER_LEN + sizeof(payload)];
    size_t packet_len = build_v4_packet(packet, sizeof(packet), TEST_PROTOCOL_A, make_v4(10, 3, 2, 1), make_v4(10, 3, 2, 2), payload, sizeof(payload));

    feed_packet(g_iface0, TEST_ETHERTYPE_IPV4, packet, packet_len);

    DMOD_TEST_EXPECT_FALSE(g_last_call.called);
}

DMOD_TEST_STEP(unregister_protocol_unregistered_is_safe)
{
    dmip_unregister_protocol(TEST_PROTOCOL_A);
    dmip_unregister_protocol(TEST_PROTOCOL_A);
}

DMOD_TEST_STEP(default_protocol_receives_unclaimed_packet)
{
    memset(&g_last_call, 0, sizeof(g_last_call));
    DMOD_TEST_EXPECT_EQ(dmip_register_default_protocol(record_call), 0);

    uint8_t payload[2] = { 5, 6 };
    uint8_t packet[DMIP_V4_HEADER_LEN + sizeof(payload)];
    size_t packet_len = build_v4_packet(packet, sizeof(packet), TEST_PROTOCOL_A, make_v4(10, 3, 3, 1), make_v4(10, 3, 3, 2), payload, sizeof(payload));

    feed_packet(g_iface0, TEST_ETHERTYPE_IPV4, packet, packet_len);

    DMOD_TEST_EXPECT_TRUE(g_last_call.called);
    DMOD_TEST_EXPECT_EQ(g_last_call.packet_len, packet_len);

    dmip_unregister_default_protocol();
}

DMOD_TEST_STEP(specific_registration_wins_over_default)
{
    memset(&g_last_call, 0, sizeof(g_last_call));
    memset(&g_default_call, 0, sizeof(g_default_call));
    DMOD_TEST_EXPECT_EQ(dmip_register_default_protocol(record_default_call), 0);
    DMOD_TEST_EXPECT_EQ(dmip_register_protocol(TEST_PROTOCOL_A, record_call), 0);

    uint8_t payload[2] = { 7, 8 };
    uint8_t packet[DMIP_V4_HEADER_LEN + sizeof(payload)];
    size_t packet_len = build_v4_packet(packet, sizeof(packet), TEST_PROTOCOL_A, make_v4(10, 3, 4, 1), make_v4(10, 3, 4, 2), payload, sizeof(payload));

    feed_packet(g_iface0, TEST_ETHERTYPE_IPV4, packet, packet_len);

    DMOD_TEST_EXPECT_TRUE(g_last_call.called);
    DMOD_TEST_EXPECT_FALSE(g_default_call.called);

    dmip_unregister_protocol(TEST_PROTOCOL_A);
    dmip_unregister_default_protocol();
}

DMOD_TEST_STEP(register_default_protocol_rejects_null_handler)
{
    DMOD_TEST_EXPECT_EQ(dmip_register_default_protocol(NULL), -EINVAL);
}

DMOD_TEST_STEP(register_default_protocol_twice_returns_eexist)
{
    DMOD_TEST_EXPECT_EQ(dmip_register_default_protocol(record_call), 0);
    DMOD_TEST_EXPECT_EQ(dmip_register_default_protocol(record_call), -EEXIST);

    dmip_unregister_default_protocol();
}

DMOD_TEST_STEP(unregister_default_protocol_stops_delivery)
{
    memset(&g_last_call, 0, sizeof(g_last_call));
    DMOD_TEST_EXPECT_EQ(dmip_register_default_protocol(record_call), 0);
    dmip_unregister_default_protocol();

    uint8_t payload[2] = { 9, 10 };
    uint8_t packet[DMIP_V4_HEADER_LEN + sizeof(payload)];
    size_t packet_len = build_v4_packet(packet, sizeof(packet), TEST_PROTOCOL_A, make_v4(10, 3, 5, 1), make_v4(10, 3, 5, 2), payload, sizeof(payload));

    /* Nobody registered, no default - dispatch_packet() must drop this
     * safely (no crash), not just "not call our handler". */
    feed_packet(g_iface0, TEST_ETHERTYPE_IPV4, packet, packet_len);

    DMOD_TEST_EXPECT_FALSE(g_last_call.called);
}

DMOD_TEST_STEP(unregister_default_protocol_unregistered_is_safe)
{
    dmip_unregister_default_protocol();
    dmip_unregister_default_protocol();
}

/* ---- Family-agnostic: send dispatch ---- */

DMOD_TEST_STEP(send_rejects_null_header)
{
    DMOD_TEST_EXPECT_EQ(dmip_send(NULL, NULL, 0, DMARP_DEFAULT_TIMEOUT_MS), -EINVAL);
}

DMOD_TEST_STEP(send_unrecognized_family_returns_einval)
{
    dmip_header_t header = { 0 };
    header.family = (dmip_family_t)7;
    DMOD_TEST_EXPECT_EQ(dmip_send(&header, NULL, 0, DMARP_DEFAULT_TIMEOUT_MS), -EINVAL);
}

DMOD_TEST_STEP(send_v6_family_returns_enosys)
{
    /* No dmip_v6_send() yet (no NDP module to resolve a next-hop MAC
     * through) - dmip_send() reports that honestly rather than pretending
     * to have sent anything. */
    dmip_header_t header = { 0 };
    header.family = dmip_family_v6;
    header.header.v6.dst = make_v6(1);
    DMOD_TEST_EXPECT_EQ(dmip_send(&header, NULL, 0, DMARP_DEFAULT_TIMEOUT_MS), -ENOSYS);
}

DMOD_TEST_STEP(send_v4_family_dispatches_to_v4_send)
{
    /* Same setup as v4_send_full_path_without_real_driver_returns_eio -
     * dmip_send() should reach the exact same -EIO by dispatching to
     * dmip_v4_send() under the hood, not by reimplementing any of it. */
    dmip_addr_t dest_net = make_v4(172, 16, 6, 0);
    dmip_addr_t netmask = make_v4(255, 255, 255, 0);
    dmroute_route_t route = dmroute_add(&dest_net, &netmask, NULL, "test0", DMROUTE_DEFAULT_METRIC, dmroute_origin_static);

    dmip_addr_t dest = make_v4(172, 16, 6, 10);
    dmnetif_mac_addr_t fake_mac = { { 0x02, 0x00, 0x00, 0x00, 0x00, 0x11 } };
    dmarp_cache_insert(g_iface0, &dest, &fake_mac);

    dmip_header_t header = { 0 };
    header.family = dmip_family_v4;
    header.header.v4.dst = dest;
    header.header.v4.protocol = DMIP_PROTO_UDP;
    header.header.v4.ttl = DMIP_DEFAULT_TTL;
    header.header.v4.identification = dmip_v4_next_identification();

    uint8_t payload[4] = { 5, 6, 7, 8 };
    DMOD_TEST_EXPECT_EQ(dmip_send(&header, payload, sizeof(payload), DMARP_DEFAULT_TIMEOUT_MS), -EIO);

    dmarp_cache_remove(g_iface0, &dest);
    dmroute_remove(route);
}

/* There is no dmip_receive() anymore - see the "Protocol dispatch"
 * section above for what replaced it (receiving is dispatched by
 * protocol number, not pulled by a generic receive call). */
