# ebus_discovery: the eBus mDNS contract as code (broker service types and the browse list,
# the TXT record of each advertised service, and the interface a port's mDNS stack
# implements). No dependency beyond the C and C++ standard libraries, and only its own
# include directory. See doc/discovery.md.
# The top-level CMakeLists.txt adds this directory; a project that wants only
# ebus_discovery can add_subdirectory(<path>/cpp-sdk/discovery) instead.
cmake_minimum_required(VERSION 3.18)
project(ebus_discovery LANGUAGES CXX)

add_library(ebus_discovery STATIC
    src/broker_discovery.cpp
    src/broker_browse.cpp
    src/txt_records.cpp
)
target_include_directories(ebus_discovery PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(ebus_discovery PUBLIC cxx_std_17)
