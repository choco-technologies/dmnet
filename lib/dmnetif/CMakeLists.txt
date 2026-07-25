# Name of the module
set(DMOD_MODULE_NAME        dmnetif)

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         1024)

# Path to the .dmr file - enables release package creation in build/packages/
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/dmnetif.dmr)

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
    src/dmnetif.c
)

target_include_directories(${DMOD_MODULE_NAME}_if INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# dmip and dmroute: in-tree siblings under dmnet/lib (added earlier via the
# top-level add_subdirectory(lib), in the order dmip -> dmroute -> dmnetif -
# see dmnet/CMakeLists.txt and lib/CMakeLists.txt), not separately published
# modules, so they're linked directly rather than through dmod_link_modules()
# (which fetches a *released* module's headers via dmf-get and would fail
# here since neither has such a release) - same convention
# tools/ifconfig/CMakeLists.txt uses for dmnetif itself.
#   dmip:    dmip_addr_t, the type dmnetif's IP/netmask/broadcast fields use
#   dmroute: dmnetif_set_ip_address() calls dmroute_add()/_remove() directly
#            to keep an interface's connected route in sync - see
#            update_connected_route() in src/dmnetif.c
target_link_libraries(${DMOD_MODULE_NAME} dmip_if dmroute_if)

# dmlist: backs the interface registry
# dmdrvi: DMDRVI_IOCTL_NET_* commands/types used to talk to a registered
#         interface's underlying device file
# dmini:  dmdrvi.h includes dmini.h (dmini_context_t appears in dmdrvi_create's
#         signature) - needed transitively even though dmnetif never calls it
# dmosi:  mutex guarding the registry
dmod_link_modules(${DMOD_MODULE_NAME}
    dmlist
    dmdrvi
    dmini
    dmosi
)

add_subdirectory(tests)