# ebus_mqtt: the MQTT transport interface and the Homie-agnostic client behavior (the
# disconnected-link hold and the reconnect order). No dependency beyond the C and C++
# standard libraries, and only its own include directory. See doc/mqtt.md.
# The top-level CMakeLists.txt adds this directory; a project that wants only ebus_mqtt
# can add_subdirectory(<path>/cpp-sdk/mqtt) instead.
cmake_minimum_required(VERSION 3.18)
project(ebus_mqtt LANGUAGES CXX)

add_library(ebus_mqtt STATIC
    src/publish_hold.cpp
)
target_include_directories(ebus_mqtt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ebus_mqtt PUBLIC cxx_std_17)
