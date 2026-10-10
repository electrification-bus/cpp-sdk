# ebus_link: property links over ebus_homie (doc/link.md). The pure functions: reference
# parsing, * matching, rounding, %-substitution, retry backoff. Only its own include
# directory and ebus_homie's.
# The top-level CMakeLists.txt adds this directory; a project that wants only ebus_link can
# add_subdirectory(<path>/cpp-sdk/link) instead, which adds ebus_homie and ebus_mqtt too.
cmake_minimum_required(VERSION 3.18)
project(ebus_link LANGUAGES CXX)

if(NOT TARGET ebus_homie)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../homie ${CMAKE_CURRENT_BINARY_DIR}/ebus_homie)
endif()

add_library(ebus_link STATIC
    src/link_core.cpp
)
target_include_directories(ebus_link PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ebus_link PUBLIC cxx_std_17)
target_link_libraries(ebus_link PUBLIC ebus_homie)
