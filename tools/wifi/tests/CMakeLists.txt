set(DMOD_MODULE_NAME        test_wifi)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)

dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    wifi_test.c
)

# `wifi` itself is never linked as a dependency here - it's invoked on
# demand through Dmod_RunModule() (see wifi_test.c), which loads/runs/unloads
# it per call.

# dmsecrets: the tool's own dependency (the vault of saved networks), listed so that
# dmf-get installs it for the run.
dmod_link_modules(test_wifi
    dmsecrets
)
