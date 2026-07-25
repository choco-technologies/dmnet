/**
 * @file dmudp_test.c
 * @brief Test steps for dmudp
 *
 * Checksum steps replicate dmudp's own pseudo-header + segment layout by
 * hand (using dmip_checksum() directly, same primitive dmudp.c itself
 * calls) to compute an expected checksum, then check
 * dmudp_v4_checksum_valid()/_v6_checksum_valid() agree - same
 * self-verification approach lib/dmip/tests/dmip_test.c uses for
 * dmip_checksum() itself.
 *
 * Send/receive steps register two "/null"-backed dmnetif fixture
 * interfaces (same pattern as lib/dmip/tests/dmip_test.c and
 * lib/dmarp/tests/dmarp_test.c) via dmod_test_setup()/_teardown(). No real
 * driver backs either fixture, so a full transmission/reception can't be
 * exercised end-to-end here - dmudp_send() is tested up through the point
 * dmip_send()/dmip_v4_send() itself can be tested without a driver (route
 * lookup, a hand-seeded ARP cache hit, segment building), failing only at
 * the final dmnetif_send(); dmudp_receive() is tested against "nothing
 * pending" (dmnetif_receive() always reports no frame without a real
 * driver).
 *
 * Every step uses a distinct destination network, since dmroute's routes
 * and dmarp's cache are both global state that outlives a single step.
 */
#include "dmod_test.h"
#include "dmudp.h"
#include "dmroute.h"
#include "dmarp.h"
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

static void write_u16_be(uint8_t* p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)(value & 0xFFu);
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

/* ---- Checksum ---- */

DMOD_TEST_STEP(v4_checksum_valid_round_trip)
{
    dmip_addr_t src_ip = make_v4(10, 0, 0, 1);
    dmip_addr_t dst_ip = make_v4(10, 0, 0, 2);

    uint8_t segment[DMUDP_HEADER_LEN + 4];
    write_u16_be(&segment[0], 1234);
    write_u16_be(&segment[2], 5678);
    write_u16_be(&segment[4], (uint16_t)sizeof(segment));
    write_u16_be(&segment[6], 0);
    segment[8] = 'p'; segment[9] = 'i'; segment[10] = 'n'; segment[11] = 'g';

    uint8_t buf[12 + sizeof(segment)];
    memcpy(&buf[0], src_ip.addr.v4, DMIP_IPV4_ADDR_LEN);
    memcpy(&buf[4], dst_ip.addr.v4, DMIP_IPV4_ADDR_LEN);
    buf[8] = 0;
    buf[9] = DMIP_PROTO_UDP;
    write_u16_be(&buf[10], (uint16_t)sizeof(segment));
    memcpy(&buf[12], segment, sizeof(segment));

    write_u16_be(&segment[6], dmip_checksum(buf, sizeof(buf)));

    DMOD_TEST_EXPECT_TRUE(dmudp_v4_checksum_valid(&src_ip, &dst_ip, segment, sizeof(segment)));

    segment[8] ^= 0xFF;
    DMOD_TEST_EXPECT_FALSE(dmudp_v4_checksum_valid(&src_ip, &dst_ip, segment, sizeof(segment)));
}

DMOD_TEST_STEP(v6_checksum_valid_round_trip)
{
    dmip_addr_t src_ip = make_v6(1);
    dmip_addr_t dst_ip = make_v6(2);

    uint8_t segment[DMUDP_HEADER_LEN + 4];
    write_u16_be(&segment[0], 1111);
    write_u16_be(&segment[2], 2222);
    write_u16_be(&segment[4], (uint16_t)sizeof(segment));
    write_u16_be(&segment[6], 0);
    segment[8] = 'p'; segment[9] = 'o'; segment[10] = 'n'; segment[11] = 'g';

    uint8_t buf[40 + sizeof(segment)];
    memcpy(&buf[0], src_ip.addr.v6, DMIP_IPV6_ADDR_LEN);
    memcpy(&buf[16], dst_ip.addr.v6, DMIP_IPV6_ADDR_LEN);
    buf[32] = 0; buf[33] = 0; buf[34] = 0; buf[35] = (uint8_t)sizeof(segment);
    buf[36] = 0; buf[37] = 0; buf[38] = 0; buf[39] = DMIP_PROTO_UDP;
    memcpy(&buf[40], segment, sizeof(segment));

    write_u16_be(&segment[6], dmip_checksum(buf, sizeof(buf)));

    DMOD_TEST_EXPECT_TRUE(dmudp_v6_checksum_valid(&src_ip, &dst_ip, segment, sizeof(segment)));

    segment[9] ^= 0xFF;
    DMOD_TEST_EXPECT_FALSE(dmudp_v6_checksum_valid(&src_ip, &dst_ip, segment, sizeof(segment)));
}

DMOD_TEST_STEP(checksum_valid_rejects_bad_arguments)
{
    dmip_addr_t v4_addr = make_v4(1, 1, 1, 1);
    dmip_addr_t v6_addr = make_v6(1);
    uint8_t segment[DMUDP_HEADER_LEN] = { 0 };

    DMOD_TEST_EXPECT_FALSE(dmudp_v4_checksum_valid(NULL, &v4_addr, segment, sizeof(segment)));
    DMOD_TEST_EXPECT_FALSE(dmudp_v4_checksum_valid(&v4_addr, &v4_addr, NULL, sizeof(segment)));
    DMOD_TEST_EXPECT_FALSE(dmudp_v4_checksum_valid(&v4_addr, &v4_addr, segment, DMUDP_HEADER_LEN - 1));
    DMOD_TEST_EXPECT_FALSE(dmudp_v4_checksum_valid(&v6_addr, &v4_addr, segment, sizeof(segment)));

    DMOD_TEST_EXPECT_FALSE(dmudp_v6_checksum_valid(&v4_addr, &v6_addr, segment, sizeof(segment)));
}

/* ---- Send ---- */

DMOD_TEST_STEP(send_rejects_bad_arguments)
{
    dmip_addr_t dst = make_v4(10, 0, 0, 1);

    DMOD_TEST_EXPECT_EQ(dmudp_send(NULL, 1, 1, NULL, 0, DMARP_DEFAULT_TIMEOUT_MS), -EINVAL);

    dmip_addr_t bad_family = dst;
    bad_family.family = (dmip_family_t)7;
    DMOD_TEST_EXPECT_EQ(dmudp_send(&bad_family, 1, 1, NULL, 0, DMARP_DEFAULT_TIMEOUT_MS), -EINVAL);
}

DMOD_TEST_STEP(send_v6_returns_enosys)
{
    /* No IPv6 send path yet (no NDP module) - dmudp_send() reports that
     * honestly rather than pretending to have sent anything. */
    dmip_addr_t dst = make_v6(9);
    DMOD_TEST_EXPECT_EQ(dmudp_send(&dst, 53, 12345, "x", 1, DMARP_DEFAULT_TIMEOUT_MS), -ENOSYS);
}

DMOD_TEST_STEP(send_no_route_returns_enetunreach)
{
    dmip_addr_t dst = make_v4(203, 0, 113, 9); /* TEST-NET-3 - no route added anywhere in this file */
    DMOD_TEST_EXPECT_EQ(dmudp_send(&dst, 53, 12345, "x", 1, DMARP_DEFAULT_TIMEOUT_MS), -ENETUNREACH);
}

DMOD_TEST_STEP(send_full_path_without_real_driver_returns_eio)
{
    dmip_addr_t dest_net = make_v4(172, 16, 9, 0);
    dmip_addr_t netmask = make_v4(255, 255, 255, 0);
    dmroute_route_t route = dmroute_add(&dest_net, &netmask, NULL, "test0", DMROUTE_DEFAULT_METRIC, dmroute_origin_static);

    dmip_addr_t dest = make_v4(172, 16, 9, 10);
    dmnetif_mac_addr_t fake_mac = { { 0x02, 0x00, 0x00, 0x00, 0x00, 0x20 } };
    dmarp_cache_insert(g_iface0, &dest, &fake_mac);

    uint8_t payload[3] = { 'h', 'i', '!' };
    DMOD_TEST_EXPECT_EQ(dmudp_send(&dest, 5353, 12345, payload, sizeof(payload), DMARP_DEFAULT_TIMEOUT_MS), -EIO);

    dmarp_cache_remove(g_iface0, &dest);
    dmroute_remove(route);
}

/* ---- Receive ---- */

DMOD_TEST_STEP(receive_rejects_bad_arguments)
{
    dmip_family_t family = dmip_family_none;
    dmip_addr_t src_ip = { 0 };
    uint16_t src_port = 0, dst_port = 0;
    uint8_t* payload = NULL;
    size_t payload_len = 0;

    DMOD_TEST_EXPECT_EQ(dmudp_receive(NULL, &family, &src_ip, &src_port, &dst_port, &payload, &payload_len), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmudp_receive(g_iface0, NULL, &src_ip, &src_port, &dst_port, &payload, &payload_len), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmudp_receive(g_iface0, &family, NULL, &src_port, &dst_port, &payload, &payload_len), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmudp_receive(g_iface0, &family, &src_ip, NULL, &dst_port, &payload, &payload_len), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmudp_receive(g_iface0, &family, &src_ip, &src_port, NULL, &payload, &payload_len), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmudp_receive(g_iface0, &family, &src_ip, &src_port, &dst_port, NULL, &payload_len), -EINVAL);
    DMOD_TEST_EXPECT_EQ(dmudp_receive(g_iface0, &family, &src_ip, &src_port, &dst_port, &payload, NULL), -EINVAL);
}

DMOD_TEST_STEP(receive_no_pending_frame_returns_eagain)
{
    dmip_family_t family = dmip_family_none;
    dmip_addr_t src_ip = { 0 };
    uint16_t src_port = 0, dst_port = 0;
    uint8_t* payload = NULL;
    size_t payload_len = 0;

    DMOD_TEST_EXPECT_EQ(dmudp_receive(g_iface0, &family, &src_ip, &src_port, &dst_port, &payload, &payload_len), -EAGAIN);
}
