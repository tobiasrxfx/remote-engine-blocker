add_library(can STATIC
    can_frame.c
    can_ids.c
    can_codec.c
)

target_include_directories(can
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(can
    PUBLIC
        common
)