# ebus_homie: the Homie 5 model over ebus_mqtt's transport interface (Device, Node,
# Property, the $state and $description lifecycle, /set dispatch, the controller, id and
# payload validation) and the log and clock hooks a port binds. Depends on ebus_mqtt and
# ArduinoJson, with only its own include directory. See doc/core.md.
# The top-level CMakeLists.txt adds this directory; a project that wants only ebus_homie
# can add_subdirectory(<path>/cpp-sdk/homie) instead, which adds ebus_mqtt too.
cmake_minimum_required(VERSION 3.18)
project(ebus_homie LANGUAGES CXX)

if(NOT TARGET ebus_mqtt)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../mqtt ${CMAKE_CURRENT_BINARY_DIR}/ebus_mqtt)
endif()

add_library(ebus_homie STATIC
    src/controller.cpp
    src/controller_inbox.cpp
    src/Device.cpp
    src/homie_clock.cpp
    src/homie_datatype.cpp
    src/homie_id.cpp
    src/homie_json.cpp
    src/homie_log.cpp
    src/homie_settable.cpp
    src/homie_topic.cpp
    src/homie_transport.cpp
    src/jsonUtils.cpp
    src/Node.cpp
    src/Property.cpp
)
target_include_directories(ebus_homie PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ebus_homie PUBLIC cxx_std_17)
target_link_libraries(ebus_homie PUBLIC ebus_mqtt)

# ArduinoJson (header-only). A parent project that already has an ArduinoJson target keeps
# it. Otherwise the release PlatformIO uses (library.json, esp32-sdk's platformio.ini) is
# fetched, pinned by hash, so both builds serialize identically; to build offline, point
# -DFETCHCONTENT_SOURCE_DIR_ARDUINOJSON=<dir> at a checkout of it.
if(NOT TARGET ArduinoJson)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)   # extracted files get the extraction time
    endif()
    include(FetchContent)
    FetchContent_Declare(ArduinoJson
        URL https://github.com/bblanchon/ArduinoJson/archive/refs/tags/v7.4.3.tar.gz
        URL_HASH SHA256=a2a8a277fc3cd0404499199bf1468682ea3688f354be0eaf6af8648366911257
    )
    FetchContent_MakeAvailable(ArduinoJson)
endif()
target_link_libraries(ebus_homie PUBLIC ArduinoJson)

# Topic root: homie/5 by default, ebus/5 with USE_EBUS_TOPIC (ebus/homie/homie.h).
# The ESP32 firmware sets it in platformio.ini's build_flags.
option(EBUS_CORE_EBUS_TOPIC "Define USE_EBUS_TOPIC (publish under ebus/5)" OFF)
if(EBUS_CORE_EBUS_TOPIC)
    target_compile_definitions(ebus_homie PUBLIC USE_EBUS_TOPIC)
endif()
