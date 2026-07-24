set(DMOD_MODULE_NAME        test_ifconfig)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)

dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    ifconfig_test.c
)

# Only dmnetif is linked - `ifconfig` itself is never linked as a dependency
# here. It's invoked on demand through Dmod_RunModule() (see
# ifconfig_test.c), which loads/runs/unloads it per call instead of holding
# it resident for the whole test run.
target_link_libraries(${DMOD_MODULE_NAME} dmnetif_if)
