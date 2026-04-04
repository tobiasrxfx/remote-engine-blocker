add_library(reb_core STATIC placeholder.c)
target_include_directories(reb_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reb_core PUBLIC common can)