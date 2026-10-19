# Name of the module
set(DMOD_MODULE_NAME        wifid)

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         4096)

# Priority for the thread of the module (should be integer)
set(DMOD_PRIORITY           0)

# Path to the .dmr file - enables release package creation in build/packages/
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/wifid.dmr)

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/wifid.c
)

# dmnetif: finds the interface and sends the Wi-Fi ioctls to its device (dmnetif_ioctl);
# dmroute: header-only - dmnetif.h needs dmroute_addr_t;
# dmdrvi:  the DMDRVI_IOCTL_WIFI_* commands and types (2.9);
# dmsecrets: the vault of the saved networks (with the device key);
# dmosi:   sleeping.
dmod_link_modules(${DMOD_MODULE_NAME}
    dmnetif
    dmroute
    dmdrvi@>=2.9
    dmsecrets
    dmosi
)

add_subdirectory(tests)
