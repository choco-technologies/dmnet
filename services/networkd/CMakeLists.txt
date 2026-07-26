# Name of the module
set(DMOD_MODULE_NAME        networkd)

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         4096)

# Priority for the thread of the module (should be integer)
set(DMOD_PRIORITY           0)

# Path to the .dmr file - enables release package creation in build/packages/
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/networkd.dmr)

# List of MAL interfaces implemented by this module (leave empty if none)
set(DMOD_MAL_IMPLS          )

#
#   dmod_add_executable - create an executable (application) module
#   it has the same signature as add_executable,
#   and can be used in the same way after the creation
#   (for example, to link libraries)
#
dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    # List of source files - can include C and C++ files
    src/networkd.c
)

# dmroute:     header-only - dmnetif.h needs dmroute_addr_t directly, even
#              though networkd.c itself never calls a dmroute_* function
# dmnetif:     dmnetif_for_each() to enumerate already-registered
#              interfaces, dmnetif_get_name()/dmnetif_is_present() for
#              pump-thread naming/shutdown - see src/networkd.c
# dmnetbridge: dmnetbridge_reset()/_handle_netif_rx() - the whole point of
#              this service
# All three are in-tree siblings under dmnet/lib (added earlier via the
# top-level add_subdirectory(lib) - see dmnet/CMakeLists.txt and
# lib/CMakeLists.txt), not separately published modules, so they're linked
# directly rather than through dmod_link_modules() (which fetches a
# *released* module's headers via dmf-get and would fail here since none
# of them has such a release) - same convention lib/dmarp/CMakeLists.txt
# uses for dmnetif.
target_link_libraries(${DMOD_MODULE_NAME} dmroute_if dmnetif_if dmnetbridge_if)

# dmlist: backs g_pumps (every spawned pump thread, for shutdown)
# dmosi:  dmosi_thread_create()/_join()/_kill()/_destroy()/_sleep() to run
#         and stop pump threads
dmod_link_modules(${DMOD_MODULE_NAME}
    dmlist
    dmosi
)

add_subdirectory(tests)
