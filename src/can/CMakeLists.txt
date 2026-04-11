add_library(can STATIC
    can_frame.c
    can_ids.c
    can_codec.c
    can_rx.c
    can_tx.c
    can_monitor.c
    can_socket_transport.c
)

target_include_directories(can
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(can
    PUBLIC
        common
)

if(WIN32)
    target_link_libraries(can PRIVATE ws2_32)
endif()