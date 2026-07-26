# Name of the module
set(DMOD_MODULE_NAME        dmudp)

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         1024)

# Path to the .dmr file - enables release package creation in build/packages/
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/dmudp.dmr)

# List of MAL interfaces implemented by this module (leave empty if none)
set(DMOD_MAL_IMPLS          )

# List of DIF interfaces implemented by this module (leave empty if none)
set(DMOD_DIF_IMPLS          )

#
#   dmod_add_library - create a library module
#   it has the same signature as add_library
#   and can be used in the same way after the creation
#   (for example, to link libraries)
#
dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    # List of source files - can include C and C++ files
    src/dmudp.c
)

target_include_directories(${DMOD_MODULE_NAME}_if INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# dmip: dmip_send() does the actual work - dmudp only builds/parses/
#       checksums the UDP segment and hands it to dmip, plus
#       dmip_checksum() for the pseudo-header checksum,
#       dmip_v4_get_source_address()/_v4_next_identification(), and
#       dmip_register_protocol()/_unregister_protocol() to receive
#       completed IP packets (see src/dmudp.c). An in-tree sibling
#       (lib/dmip - added earlier via the top-level add_subdirectory(lib)
#       - see dmnet/CMakeLists.txt), not a separately published module, so
#       it's linked directly rather than through dmod_link_modules()
#       (which fetches a *released* module's headers via dmf-get and would
#       fail here since it has no such release).
target_link_libraries(${DMOD_MODULE_NAME} dmip_if)

# dmnetif: dmnetif_iface_t, dmudp_receive()'s optional out_iface parameter
#          - a separately published module, fetched via dmod_link_modules().
# dmroute: dmip_addr_t's real definition (dmroute_addr_t) - dmip_if alone
#          doesn't carry its include dir. Also a separately published module.
# dmlist:  backs dmudp's own receive queue (see src/dmudp.c)
# dmosi:   mutex guarding that queue, plus a semaphore dmudp_receive() waits
#          on and dmudp_handle_ip_packet() posts
dmod_link_modules(${DMOD_MODULE_NAME}
    dmnetif
    dmroute
    dmlist
    dmosi
)

add_subdirectory(tests)
