# Name of the module
set(DMOD_MODULE_NAME        dmarp)

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         1024)

# Path to the .dmr file - enables release package creation in build/packages/
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/dmarp.dmr)

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
    src/dmarp.c
)

target_include_directories(${DMOD_MODULE_NAME}_if INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# dmip:    dmip_addr_t, the address type dmarp resolves
# dmnetif: dmarp sends/receives raw ARP frames through dmnetif_send()/
#          _receive() on a specific interface, and reads that interface's
#          own MAC/IP address to build a request - see src/dmarp.c.
#          Nothing depends on dmarp in turn, so this is a plain one-way
#          dependency, not a DIF/MAL.
# Both are in-tree siblings (lib/dmip, lib/dmnetif - added earlier via the
# top-level add_subdirectory(lib) - see dmnet/CMakeLists.txt), not
# separately published modules, so they're linked directly rather than
# through dmod_link_modules() (which fetches a *released* module's headers
# via dmf-get and would fail here since neither has such a release) - same
# convention tools/ifconfig/CMakeLists.txt uses for dmnetif.
target_link_libraries(${DMOD_MODULE_NAME} dmip_if dmnetif_if)

# dmlist: backs the ARP cache
# dmosi:  mutex guarding the cache, plus dmosi_get_tick_count()/
#         _thread_sleep() for dmarp_resolve()'s reply poll loop
dmod_link_modules(${DMOD_MODULE_NAME}
    dmlist
    dmosi
)

add_subdirectory(tests)
