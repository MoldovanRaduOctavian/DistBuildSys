#ifndef SERVER_HPP
#define SERVER_HPP

#include <boost/asio.hpp>
#include <google/protobuf/message.h>

#include "client_views_storage.hpp"
#include "server_session.hpp"

class Server {

public:
    
    Server
        (
        boost::asio::io_context &
                            io_ctx,
        const std::string & server_ip,
        uint16_t            server_port
        ) :
        _io_ctx(io_ctx),
        _tcp_acceptor
            (
            io_ctx,
            boost::asio::ip::tcp::endpoint
                (
                boost::asio::ip::make_address(server_ip),
                server_port
                )
            ),
        _server_ip(server_ip),
        _server_port(server_port)
    {};

    boost::asio::awaitable<void> listen_for_connections(); 

private:

    boost::asio::io_context &       _io_ctx;
    boost::asio::ip::tcp::acceptor  _tcp_acceptor;
    
    /* The server owns the client views storage */
    ClientViewsStorage              _client_views;  
    std::string                     _server_ip;
    uint16_t                        _server_port;

};

#endif /* SERVER_HPP */

