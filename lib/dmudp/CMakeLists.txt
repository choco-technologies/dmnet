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

# dmip:    dmip_v4_send()/_receive()/_v6_receive() do the actual work -
#          dmudp only builds/parses/checksums the UDP segment and hands it
#          to dmip, plus dmip_checksum() for the pseudo-header checksum and
#          dmip_v4_get_source_address()/_v4_next_identification()
# dmroute: dmip_addr_t's real definition (dmroute_addr_t) - dmip_if alone
#          doesn't carry its include dir
# dmnetif: dmnetif_iface_t, the interface dmudp_receive() polls
# All three are in-tree siblings (lib/dmip, lib/dmroute, lib/dmnetif -
# added earlier via the top-level add_subdirectory(lib) - see
# dmnet/CMakeLists.txt), not separately published modules, so they're
# linked directly rather than through dmod_link_modules() (which fetches a
# *released* module's headers via dmf-get and would fail here since none
# of them has such a release) - same convention tools/ifconfig/CMakeLists.txt
# uses for dmnetif.
target_link_libraries(${DMOD_MODULE_NAME} dmip_if dmroute_if dmnetif_if)

add_subdirectory(tests)
