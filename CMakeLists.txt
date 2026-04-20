cmake_minimum_required(VERSION 3.20)

project(REB
    VERSION 0.1.0
    DESCRIPTION "Remote Engine Blocker"
    LANGUAGES C)

if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(WARNING "This project is standardized on the Ninja generator.")
    message(WARNING "Please configure using: cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)

add_custom_target(requirements_audit
    COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/tools/requirements_audit.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running requirements coverage audit"
)
