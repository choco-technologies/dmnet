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

# dmroute has no dependency on any other in-tree module - it owns the
# address type (dmroute_addr_t) rather than borrowing it from dmip, and has
# no dependency on dmnetif either - see docs/dmroute.md for why.

# dmlist: backs the route table
# dmosi:  mutex guarding the route table
dmod_link_modules(${DMOD_MODULE_NAME}
    dmlist
    dmosi
)

add_subdirectory(tests)
