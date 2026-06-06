#include <cstring>
#include <iostream>
#include <thread>

#include <boost/asio.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "server.hpp"
#include "client.hpp"
#include "yaml_cfg.hpp"

int main(int argc, char ** argv) {
    
    boost::asio::io_context io_ctx;

    YamlConfig & distbuild_cfg = YamlConfig::instance();
    if (argc == 3 && std::strcmp(argv[1], "server") == 0) {
        std::cout << "Welcome to the server!\n";
        distbuild_cfg.load_cfg(argv[2]);
        Server server
            (
            io_ctx, 
            boost::uuids::string_generator()(distbuild_cfg.node_uuid), 
            distbuild_cfg.node_ip, 
            distbuild_cfg.node_main_port,
            distbuild_cfg.node_advertising_port,
            distbuild_cfg.compiler_threads
            );
        server.start_server();

        std::vector<std::thread> io_ctx_threads;
        for (size_t th_idx = 0; th_idx < 4; ++th_idx) {
            io_ctx_threads.emplace_back([&io_ctx]{
                io_ctx.run();
            });
        }
        
        for (std::thread & th : io_ctx_threads) {
            th.join();
        }

    }
     
    else if (argc == 3 && std::strcmp(argv[1], "client") == 0) {
        std::cout << "Welcome to the client!\n";
        distbuild_cfg.load_cfg(argv[2]);
#if 0
        compiler_call.initialize_compiler_call
            (
            "/home/radu/distbuild/artifacts", 
            {"/usr/bin/clang++", "-DBOOST_ATOMIC_DYN_LINK", "-DBOOST_ATOMIC_NO_LIB", "-DBOOST_FILESYSTEM_DYN_LINK", 
            "-g", "-Wall", "-Wextra", "-Wshadow", "-std=c++20", "-MD", "-MT", 
            "CMakeFiles/distbuild_daemon.dir/src/client_session.cpp.o", "-o", 
            "CMakeFiles/distbuild_daemon.dir/src/client_session.cpp.o", "-c", 
            "/home/radu/distbuild/DistBuildSys/src/client_session.cpp"}
            );

#endif 

        Client client
            (
            io_ctx,
            boost::uuids::string_generator()(distbuild_cfg.node_uuid),
            distbuild_cfg.node_advertising_port,
            distbuild_cfg.compiler_threads
            );
        /*
        boost::asio::co_spawn
            (
            io_ctx,
            client.connect_to_server("127.0.0.1", 8082),
            boost::asio::detached
            );
        */

        std::vector<std::thread> io_ctx_threads;
        for (size_t th_idx = 0; th_idx < 8; ++th_idx) {
            io_ctx_threads.emplace_back([&io_ctx]{
                io_ctx.run();
            });
        }
        
        for (std::thread & th : io_ctx_threads) {
            th.join();
        }
        

    }


    return 0;

}
