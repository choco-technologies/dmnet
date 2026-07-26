set(DMOD_MODULE_NAME        test_dmip)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)

dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    dmip_test.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# dmarp: the send/receive test steps seed dmarp cache entries by hand (see
#        dmip_test.c's file comment).
# dmnetbridge: feed_packet() drives dmip's packet_received DIF
#              implementation directly via Dmod_GetNextDifModule()/
#              Dmod_GetDifFunction(), the same way
#              dmnetbridge_handle_netif_rx() itself does.
target_link_libraries(${DMOD_MODULE_NAME} dmip_if dmnetbridge_if)

# dmnetif: the send/receive test steps register dmnetif fixture interfaces
#          by hand (see dmip_test.c's file comment).
# dmroute: routes are seeded by hand - see dmip_test.c's file comment.
dmod_link_modules(${DMOD_MODULE_NAME}
    dmarp
    dmnetif
    dmroute
)
