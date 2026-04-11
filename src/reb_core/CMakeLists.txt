add_library(reb_core STATIC
    reb_core.c
    reb_state_machine.c
    reb_rules.c
    reb_security.c
    reb_persistence.c
    reb_logger.c
    reb_can_adapter.c
)

target_include_directories(reb_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(reb_core
    PUBLIC
        common
        can
)