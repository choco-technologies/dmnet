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