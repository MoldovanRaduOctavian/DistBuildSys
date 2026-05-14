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

# add_executable(distbuild_wrapper src/compiler_wrapper_main.cpp)
# target_link_libraries(distbuild_wrapper
#    PRIVATE
#        Boost::system
#        Threads::Threads
#        spdlog::spdlog_header_only
#)

add_executable(distbuild_daemon 
    src/daemon_main.cpp
    src/includes_rework.cpp
    src/compiler_call.cpp
    src/lru_storage.cpp
    src/source_storage.cpp
    src/object_storage.cpp
    src/client.cpp
    src/client_session.cpp
    src/dep_files.cpp
    src/unix_ipc_socket.cpp
    src/compiler_call.cpp
    src/server.cpp
    src/server_session.cpp
    src/proto_io.cpp
    src/client_view.cpp
    src/client_views_storage.cpp
    src/compiler_manager.cpp
    src/distbuild_messages.pb.cc
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
    src/asio_test_main.cpp
    src/includes_rework.cpp
    src/compiler_call.cpp
    src/lru_storage.cpp
    src/source_storage.cpp
    src/object_storage.cpp
    src/client.cpp
    src/client_session.cpp
    src/dep_files.cpp
    src/unix_ipc_socket.cpp
    src/compiler_call.cpp
    src/server.cpp
    src/server_session.cpp
    src/proto_io.cpp
    src/client_view.cpp
    src/client_views_storage.cpp
    src/compiler_manager.cpp
    src/distbuild_messages.pb.cc

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

add_executable(unix_ipc_server
    src/unix_server_test.cpp
)

target_link_libraries(unix_ipc_server
    PRIVATE
    Boost::system
    Boost::regex
    Boost::filesystem
    Threads::Threads
    spdlog::spdlog_header_only
    ${Protobuf_LIBRARIES}
)

add_executable(unix_ipc_client
    src/unix_client_test.cpp
)

target_link_libraries(unix_ipc_client
    PRIVATE
    Boost::system
    Boost::regex
    Boost::filesystem
    Threads::Threads
    spdlog::spdlog_header_only
    ${Protobuf_LIBRARIES}
)

add_executable(distbuild_wrapper
    src/distbuild_wrapper.cpp
)

target_link_libraries(unix_ipc_client
    PRIVATE
    Boost::system
    Boost::regex
    Boost::filesystem
    Threads::Threads
    spdlog::spdlog_header_only
    ${Protobuf_LIBRARIES}
)


