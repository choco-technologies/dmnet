# Name of the module
set(DMOD_MODULE_NAME        wifi)

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         2048)

# Priority for the thread of the module (should be integer)
set(DMOD_PRIORITY           0)

# Path to the .dmr file - enables release package creation in build/packages/
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/wifi.dmr)

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/wifi.c
)

# dmdrvi: the standard DMDRVI_IOCTL_WIFI_* commands and their types (0x700
#         range, dmdrvi 2.9) - the tool knows no driver, only the contract.
# dmosi:  sleep while waiting for a connection.
dmod_link_modules(${DMOD_MODULE_NAME}
    dmdrvi@>=2.9
    dmsecrets
    dmosi
)

add_subdirectory(tests)
