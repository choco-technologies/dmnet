# Name of the module
set(DMOD_MODULE_NAME        ip)

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         1024)

# Priority for the thread of the module (should be integer)
set(DMOD_PRIORITY           0)

# Path to the .dmr file - enables release package creation in build/packages/
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/ip.dmr)

#
#   dmod_add_executable - create an executable module
#   it has the same signature as add_executable
#   and can be used in the same way after the creation
#   (for example, to link libraries)
#
dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/ip.c
)

# dmnetif: validates `dev <iface>` on `ip route add` (dmroute itself
#          can't - see dmroute.h's file comment).
# dmroute: the routing table this tool lists/controls (`ip route ...`),
#          and the address type (dmroute_addr_t) both its and dmnetif's
#          APIs use.
# Both are separately published modules, fetched via dmod_link_modules().
dmod_link_modules(${DMOD_MODULE_NAME}
    dmnetif
    dmroute
)

add_subdirectory(tests)
