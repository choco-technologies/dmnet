# Name of the module
set(DMOD_MODULE_NAME        dmroute)

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         1024)

# Path to the .dmr file - enables release package creation in build/packages/
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/dmroute.dmr)

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
    src/dmroute.c
)

target_include_directories(${DMOD_MODULE_NAME}_if INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# dmip: in-tree sibling under dmnet/lib (added earlier via the top-level
# add_subdirectory(lib) - see dmnet/CMakeLists.txt), not a separately
# published module, so it's linked directly rather than through
# dmod_link_modules() (which fetches a *released* module's headers via
# dmf-get and would fail here since dmip has no such release) - same
# convention tools/ifconfig/CMakeLists.txt uses for dmnetif. dmroute has no
# dependency on dmnetif itself - see docs/dmroute.md for why.
target_link_libraries(${DMOD_MODULE_NAME} dmip_if)

# dmlist: backs the route table
# dmosi:  mutex guarding the route table
dmod_link_modules(${DMOD_MODULE_NAME}
    dmlist
    dmosi
)

add_subdirectory(tests)
