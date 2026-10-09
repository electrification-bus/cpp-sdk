# Builds the core without PlatformIO, for Zephyr, embedded Linux, or a host check.
# Usage from a parent project: add_subdirectory(path/to/cpp-sdk) and link ebus_core, the
# umbrella, or one component target (ebus_mqtt, ebus_discovery, ebus_homie).
cmake_minimum_required(VERSION 3.18)
project(ebus_core LANGUAGES CXX)

add_subdirectory(mqtt)
add_subdirectory(discovery)
add_subdirectory(homie)

# The umbrella: the three components, plus include/, which holds the headers at the paths
# they had before the split (each forwards to its component).
add_library(ebus_core INTERFACE)
target_include_directories(ebus_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ebus_core INTERFACE ebus_mqtt ebus_discovery ebus_homie)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_ebus_core_top_level ON)
else()
    set(_ebus_core_top_level OFF)
endif()

if(_ebus_core_top_level AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ebus_mqtt PRIVATE -Wall -Wextra)
    target_compile_options(ebus_discovery PRIVATE -Wall -Wextra)
    target_compile_options(ebus_homie PRIVATE -Wall -Wextra)
endif()

# Compiles every public header in a translation unit of its own, so a header that needs
# something it does not include (or includes a platform header) fails here. A component's
# headers are compiled against that component's target alone.
option(EBUS_CORE_CHECK_HEADERS "Compile each public header on its own" ${_ebus_core_top_level})
if(EBUS_CORE_CHECK_HEADERS)
    function(ebus_header_check target include_dir)
        file(GLOB_RECURSE _headers RELATIVE ${include_dir} CONFIGURE_DEPENDS ${include_dir}/*.h)
        set(_tus "")
        foreach(_header IN LISTS _headers)
            string(MAKE_C_IDENTIFIER ${_header} _tu_name)
            set(_tu ${CMAKE_CURRENT_BINARY_DIR}/header_check/${target}/${_tu_name}.cpp)
            file(CONFIGURE OUTPUT ${_tu} CONTENT "#include <${_header}>\n")
            list(APPEND _tus ${_tu})
        endforeach()
        add_library(${target}_header_check OBJECT ${_tus})
        target_link_libraries(${target}_header_check PRIVATE ${target})
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target}_header_check PRIVATE -Wall -Wextra)
        endif()
    endfunction()
    ebus_header_check(ebus_mqtt ${CMAKE_CURRENT_SOURCE_DIR}/mqtt/include)
    ebus_header_check(ebus_discovery ${CMAKE_CURRENT_SOURCE_DIR}/discovery/include)
    ebus_header_check(ebus_homie ${CMAKE_CURRENT_SOURCE_DIR}/homie/include)
    ebus_header_check(ebus_core ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()

# The Unity suites in test/, one CTest test each. Off when the core is a subproject.
option(EBUS_CORE_TESTS "Build the core's unit tests (fetches Unity)" ${_ebus_core_top_level})
if(EBUS_CORE_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
