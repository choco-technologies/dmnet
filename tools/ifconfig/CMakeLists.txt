# Name of the module
set(DMOD_MODULE_NAME        ifconfig)

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         1024)

# Priority for the thread of the module (should be integer)
set(DMOD_PRIORITY           0)

# Path to the .dmr file - enables release package creation in build/packages/
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/ifconfig.dmr)

#
#   dmod_add_executable - create an executable module
#   it has the same signature as add_executable
#   and can be used in the same way after the creation
#   (for example, to link libraries)
#
dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/ifconfig.c
)

# dmnetif: the network interface manager this tool lists/controls. It's an
# in-tree sibling (lib/dmnetif, added earlier via the top-level
# add_subdirectory(lib) - see dmnet/CMakeLists.txt), not a separately
# published module, so it's linked directly rather than through
# dmod_link_modules() (which fetches a *released* module's headers via
# dmf-get and would fail here since dmnetif has no such release).
# dmip: dmip_addr_t, the type dmnetif's IP/netmask/broadcast fields use -
#       dmnetif_if alone doesn't carry dmip's include dir.
target_link_libraries(${DMOD_MODULE_NAME} dmnetif_if dmip_if)

add_subdirectory(tests)
