# Library-type DMOD modules that live under dmnet/lib/.
#
# Each subdirectory is an independent DMOD module (its own CMakeLists.txt
# calling dmod_add_library()), added here rather than directly from the
# top-level CMakeLists.txt.

# dmroute has no dependencies of its own (see lib/dmroute/docs/dmroute.md) -
# it owns the shared dmroute_addr_t type outright rather than borrowing it
# from another module - so it must be added before everything else here.
add_subdirectory(dmroute)

# dmnetif depends on dmroute (dmroute_addr_t, plus keeping an interface's
# connected route in sync - see lib/dmnetif/src/dmnetif.c), so it must be
# added after it.
add_subdirectory(dmnetif)

# dmarp depends on dmroute (dmroute_addr_t) and dmnetif (sends/receives ARP
# frames through dmnetif_send()/_receive() - see lib/dmarp/src/dmarp.c), so
# it must be added after both.
add_subdirectory(dmarp)

# dmip depends on dmroute (route lookup for its send path), dmnetif (frame
# I/O), and dmarp (resolving a destination MAC before sending - see
# lib/dmip/src/dmip.c) - it must be added after all three. dmip.h also
# re-exports dmroute's address type as dmip_addr_t for source
# compatibility (see lib/dmip/docs/dmip.md).
add_subdirectory(dmip)

# dmudp depends on dmip (dmip_v4_send()/_receive()/_v6_receive(),
# dmip_checksum()), dmroute (dmip_addr_t's real definition), and dmnetif
# (dmnetif_iface_t) - see lib/dmudp/docs/dmudp.md - so it must be added last.
add_subdirectory(dmudp)
