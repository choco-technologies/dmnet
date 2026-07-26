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

# dmarp depends on dmroute (dmroute_addr_t) and dmnetif (sends ARP frames
# through dmnetif_send() and reads an interface's own MAC/IP address - see
# lib/dmarp/src/dmarp.c), so it must be added after both.
add_subdirectory(dmarp)

# dmnetbridge depends on dmroute (route lookup for its send path), dmnetif
# (frame I/O, the interface registry), and dmarp (resolving a destination
# MAC before sending, and opportunistic ARP learning via
# dmarp_note_frame() - see lib/dmnetbridge/src/dmnetbridge.c) - it must be
# added after all three.
add_subdirectory(dmnetbridge)

# dmip depends on dmnetbridge (dmnetbridge_send()/_get_source_address() for
# its own send path, and implements dmnetbridge's packet_received DIF for
# receiving - see lib/dmip/src/dmip.c) - it must be added after it. dmip.h
# also re-exports dmroute's address type as dmip_addr_t for source
# compatibility (see lib/dmip/docs/dmip.md).
add_subdirectory(dmip)

# dmudp depends on dmip (dmip_send()/_receive()), dmroute (dmip_addr_t's
# real definition), and dmnetif (dmnetif_iface_t, for the optional
# out_iface parameter) - see lib/dmudp/docs/dmudp.md - so it must be added
# last.
add_subdirectory(dmudp)
