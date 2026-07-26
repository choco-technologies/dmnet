# Application-type DMOD modules that live under dmnet/services/.

# networkd runs dmnetbridge_handle_netif_rx() in its own thread for every
# interface dmnetif already knows about at startup - see
# services/networkd/src/networkd.c. Depends on dmnetbridge (and,
# transitively through it, dmnetif/dmroute/dmarp), all added earlier via
# the top-level add_subdirectory(lib) - see dmnet/CMakeLists.txt.
add_subdirectory(networkd)
