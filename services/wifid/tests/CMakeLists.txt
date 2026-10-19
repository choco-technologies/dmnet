set(DMOD_MODULE_NAME        test_wifid)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)

dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    wifid_test.c
)

# `wifid` itself is never linked as a dependency here - it is run on demand
# through Dmod_RunModule() (see wifid_test.c).
# dmnetif registers the fixture interface; dmroute: header-only (dmnetif.h);
# dmsecrets: wifid's own dependency, listed so that dmf-get installs it for the run.
dmod_link_modules(${DMOD_MODULE_NAME}
    dmnetif
    dmroute
    dmsecrets
)
