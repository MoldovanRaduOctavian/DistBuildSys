#include <cstring>
#include <iostream>
#include <thread>

#include <boost/asio.hpp>

#include "server.hpp"
#include "client.hpp"

int main(int argc, char ** argv) {
    
    boost::asio::io_context io_ctx;
    if (argc == 2 && std::strcmp(argv[1], "server") == 0) {
        std::cout << "Welcome to the server!\n";
        Server server(io_ctx, "127.0.0.1", 8082);
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
        /*
        Client client(io_ctx);
        boost::asio::co_spawn
            (
            io_ctx,
            client.connect_to_server("127.0.0.1", 8082),
            boost::asio::detached
            );
        */
        io_ctx.run();
    }


    return 0;

}
