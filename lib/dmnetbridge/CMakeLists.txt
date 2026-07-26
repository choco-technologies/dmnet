# Name of the module
set(DMOD_MODULE_NAME        dmnetbridge)

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         1024)

# Path to the .dmr file - enables release package creation in build/packages/
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/dmnetbridge.dmr)

# List of MAL interfaces implemented by this module (leave empty if none)
set(DMOD_MAL_IMPLS          )

# List of DIF interfaces implemented by this module (leave empty if none) -
# dmnetbridge owns the packet_received DIF (declared in include/dmnetbridge.h)
# that upper-layer protocol modules (dmip today) implement.
set(DMOD_DIF_IMPLS          dmnetbridge)

#
#   dmod_add_library - create a library module
#   it has the same signature as add_library
#   and can be used in the same way after the creation
#   (for example, to link libraries)
#
dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    # List of source files - can include C and C++ files
    src/dmnetbridge.c
)

target_include_directories(${DMOD_MODULE_NAME}_if INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# dmnetif: dmnetif_iface_t, frame send/receive, the interface registry - a
#          separately published module, fetched via dmod_link_modules().
# dmroute: dmroute_addr_t, route lookup for the send path - also a
#          separately published module.
# dmlist:  backs the "which interfaces currently have a pump loop running"
#          bookkeeping dmnetbridge_reset() clears
# dmosi:   dmnetbridge itself needs no thread/mutex of its own beyond that
#          list's guard - see src/dmnetbridge.c
dmod_link_modules(${DMOD_MODULE_NAME}
    dmarp
    dmnetif
    dmroute
    dmlist
    dmosi
)

add_subdirectory(tests)
