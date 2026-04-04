add_library(can STATIC placeholder.c)
target_include_directories(can PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(can PUBLIC common)