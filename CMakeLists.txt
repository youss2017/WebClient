cmake_minimum_required(VERSION 3.00)

project(WebClient VERSION 1.0)

set(CMAKE_CXX_STANDARD 20)

file(GLOB_RECURSE SOURCES "src/*.cpp")

include_directories(
        vendor/Utility/
)

if (WIN32)
else()
add_link_options(-pthread)
endif()

add_executable(WebClient ${SOURCES}
        src/vendor.cpp
        src/file_transfer.cpp
        src/json_parser.cpp
        src/json_parser.h
        src/web_server.cpp
        src/web_server.h
        src/http_header.cpp
        src/http_header.h
        src/web_packet.cpp
        src/web_packet.h
        src/sys_info.cpp
        src/sys_info.h)
