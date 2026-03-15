######################################################################
#
# DISTRIBUTED BUILDS 
#
######################################################################

cmake_minimum_required(VERSION 3.16)
project(distbuild_prj LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -Wextra -Wpedantic -Wshadow")

set(BOOST_ROOT /usr/local)
set(Boost_NO_SYSTEM_PATHS ON)

find_package(Boost REQUIRED COMPONENTS system)
find_package(Boost REQUIRED COMPONENTS regex)
find_package(Boost REQUIRED COMPONENTS filesystem)

include(FindProtobuf)
find_package(Protobuf REQUIRED)

find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)

add_executable(distbuild_wrapper src/compiler_wrapper_main.cpp)
target_link_libraries(distbuild_wrapper
    PRIVATE
        Boost::system
        Threads::Threads
        spdlog::spdlog_header_only
)

add_executable(distbuild_daemon 
    src/daemon_main.cpp
    src/includes_rework.cpp
    src/compiler_call.cpp
    src/lru_storage.cpp
    src/source_storage.cpp
    src/object_storage.cpp
    src/client.cpp
    src/server.cpp
    src/proto_io.cpp
    src/filetransfer_messages.pb.cc    
)
target_link_libraries(distbuild_daemon
    PRIVATE
    Boost::system
    Boost::regex
    Boost::filesystem
    Threads::Threads
    spdlog::spdlog_header_only
    ${Protobuf_LIBRARIES}
)

add_executable(asio_test 
    src/client.cpp
    src/server.cpp
    src/proto_io.cpp
    src/asio_test_main.cpp
    src/filetransfer_messages.pb.cc    
)
target_link_libraries(asio_test
    PRIVATE
    Boost::system
    Boost::regex
    Boost::filesystem
    Threads::Threads
    spdlog::spdlog_header_only
    ${Protobuf_LIBRARIES}
)

