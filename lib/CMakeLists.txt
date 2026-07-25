# Library-type DMOD modules that live under dmnet/lib/.
#
# Each subdirectory is an independent DMOD module (its own CMakeLists.txt
# calling dmod_add_library()), added here rather than directly from the
# top-level CMakeLists.txt.

# dmip has no dependencies of its own (see lib/dmip/docs/dmip.md) and is
# depended on by both dmnetif and dmroute for the shared dmip_addr_t type,
# so it must be added before either of them.
add_subdirectory(dmip)

# dmroute depends only on dmip (not dmnetif - see lib/dmroute/docs/dmroute.md
# for why), so it can be added before or after dmnetif on its own account,
# but must come before dmnetif here since dmnetif depends on *it*
# (dmnetif_set_ip_address() calls dmroute_add()/_remove() directly).
add_subdirectory(dmroute)

# dmnetif depends on dmip (dmip_addr_t) and dmroute (keeps an interface's
# connected route in sync - see lib/dmnetif/src/dmnetif.c), so it must be
# added after both.
add_subdirectory(dmnetif)

# dmarp depends on dmip (dmip_addr_t) and dmnetif (sends/receives ARP
# frames through dmnetif_send()/_receive() - see lib/dmarp/src/dmarp.c),
# so it must be added last. Nothing depends on dmarp in turn.
add_subdirectory(dmarp)
