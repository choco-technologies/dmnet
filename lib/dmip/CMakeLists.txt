# Name of the module
set(DMOD_MODULE_NAME        dmip)

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         1024)

# Path to the .dmr file - enables release package creation in build/packages/
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/dmip.dmr)

# List of MAL interfaces implemented by this module (leave empty if none)
set(DMOD_MAL_IMPLS          )

# List of DIF interfaces implemented by this module (leave empty if none) -
# dmip implements dmnetbridge's packet_received DIF (see src/dmip.c).
set(DMOD_DIF_IMPLS          dmnetbridge)

#
#   dmod_add_library - create a library module
#   it has the same signature as add_library
#   and can be used in the same way after the creation
#   (for example, to link libraries)
#
dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    # List of source files - can include C and C++ files
    src/dmip.c
)

target_include_directories(${DMOD_MODULE_NAME}_if INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# dmnetbridge: dmnetbridge_send()/_get_source_address()/_get_mtu() for
# dmip_v4_send(), and the packet_received DIF declaration dmip implements
# - see src/dmip.c. An in-tree sibling under dmnet/lib (added earlier via
# the top-level add_subdirectory(lib) - see dmnet/CMakeLists.txt and
# lib/CMakeLists.txt), not a separately published module, so it's linked
# directly rather than through dmod_link_modules() (which fetches a
# *released* module's headers via dmf-get and would fail here since it has
# no such release).
target_link_libraries(${DMOD_MODULE_NAME} dmnetbridge_if)

# dmnetif: header-only - dmip.h needs dmnetif_iface_t for the optional
#          out_iface parameter on dmip_v4_receive()/_v6_receive()/
#          _receive(), even though dmip.c itself never calls a dmnetif_*
#          function directly anymore. A separately published module,
#          fetched via dmod_link_modules().
# dmroute: header-only - dmip.h needs dmroute_addr_t (re-exported as
#          dmip_addr_t) directly, even though dmip.c itself no longer
#          calls any dmroute_* function (that moved to dmnetbridge - see
#          src/dmip.c's top comment)
# dmlist:  backs the fragment reassembly table
# dmosi:   mutexes guarding reassembly/identification state, plus
#          dmosi_get_tick_count() for reassembly timeouts - see
#          src/dmip.c's "Reassembly" section
dmod_link_modules(${DMOD_MODULE_NAME}
    dmnetif
    dmroute
    dmlist
    dmosi
)

add_subdirectory(tests)
