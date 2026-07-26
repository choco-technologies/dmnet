set(DMOD_MODULE_NAME        test_networkd)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)

dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    networkd_test.c
)

# dmnetbridge: dmnetbridge_reset()/_handle_netif_rx() - see networkd_test.c
target_link_libraries(${DMOD_MODULE_NAME} dmnetbridge_if)

# dmnetif: registers the fixture interface, dmnetif_for_each()
# dmroute: header-only - dmnetif.h needs dmroute_addr_t directly
# dmosi: dmosi_thread_create()/_kill()/_destroy()/_sleep() to drive a
#        background pump thread - see networkd_test.c
dmod_link_modules(${DMOD_MODULE_NAME}
    dmnetif
    dmroute
    dmosi
)
