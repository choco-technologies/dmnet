set(DMOD_MODULE_NAME        test_ip)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)

dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    ip_test.c
)

# dmroute/dmnetif are linked directly - `ip` itself is never linked as a
# dependency here. It's invoked on demand through Dmod_RunModule() (see
# ip_test.c and tools/ifconfig/tests/ifconfig_test.c for the same
# pattern), which loads/runs/unloads it per call instead of holding it
# resident for the whole test run.
# dmroute_addr_t (from dmroute_if) is used directly to build test addresses.
target_link_libraries(${DMOD_MODULE_NAME} dmroute_if dmnetif_if)
