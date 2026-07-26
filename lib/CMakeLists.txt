# dmnetbridge depends on dmroute (route lookup for its send path), dmnetif
# (frame I/O, the interface registry), and dmarp (resolving a destination
# MAC before sending, and opportunistic ARP learning via
# dmarp_note_frame() - see lib/dmnetbridge/src/dmnetbridge.c) - it must be
# added after dmarp (its only remaining in-tree dependency).
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
