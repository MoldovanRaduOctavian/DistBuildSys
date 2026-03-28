#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <google/protobuf/message.h>

class Client {

public:

    Client
        (
        boost::asio::io_context & 
                            io_ctx,
        const std::string & server_ip,
        uint16_t            server_port
        ) :
        _io_ctx(io_ctx),
        _tcp_socket(io_ctx),
        _server_ip(server_ip),
        _server_port(server_port),
        _active_session(false)
        {};

    boost::asio::awaitable<void> connect_to_server();

private:
    
    boost::asio::io_context &       _io_ctx;
    boost::asio::ip::tcp::socket    _tcp_socket;
    std::string                     _server_ip;
    uint16_t                        _server_port;
    bool                            _active_session;

};

#endif /* CLIENT_HPP */

