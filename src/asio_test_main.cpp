#include <cstring>
#include <iostream>

#include <boost/asio.hpp>

#include "server.hpp"
#include "client.hpp"

int main(int argc, char ** argv) {
    
    boost::asio::io_context io_ctx;
    if (argc == 2 && std::strcmp(argv[1], "server") == 0) {
        std::cout << "Welcome to the server!\n";
        Server server(io_ctx, "127.0.0.1", 8082);
        boost::asio::co_spawn
            (
            io_ctx, 
            server.listen_for_connections(), 
            boost::asio::detached
            );

        io_ctx.run();
    }
     
    else if (argc == 2 && std::strcmp(argv[1], "client") == 0) {
        std::cout << "Welcome to the client!\n";
        Client client(io_ctx, "127.0.0.1", 8082);
        boost::asio::co_spawn
            (
            io_ctx,
            client.connect_to_server(),
            boost::asio::detached
            );

        io_ctx.run();
    }


    return 0;

}
