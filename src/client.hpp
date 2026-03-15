#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <google/protobuf/message.h>

#include "filetransfer_messages.pb.h"

#include "proto_io.hpp"

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
    {
        // I will receive the IP address of the available
        // "server" nodes in the advertising broadcast
        // UDP packet
        // I do not even need a resolver to be honest
        // _endpoints = _tcp_resolver.resolve(server_host, server_port);

    };

    boost::asio::awaitable<void> connect_to_server() {
        boost::asio::ip::tcp::endpoint server_endpoint
            (
            boost::asio::ip::make_address(_server_ip),
            _server_port 
            );

        co_await _tcp_socket.async_connect(server_endpoint, boost::asio::use_awaitable);

        filetransfer::ClientMessage client_message;
        client_message.mutable_file_request()->set_filename("HELLO SERVER");
        client_message.mutable_file_request()->set_filesize(1024);
        co_await proto_io::send_msg(_tcp_socket, client_message);

        filetransfer::ServerMessage server_message;
        // co_await proto_io::receive_msg(_tcp_socket, server_message);
        // std::cout << "WHAT THE SERVER SENT: " << server_message.upload_status().filename() << '\n';

    }

private:
    
    boost::asio::io_context &       _io_ctx;
    boost::asio::ip::tcp::socket    _tcp_socket;
    std::string                     _server_ip;
    uint16_t                        _server_port;
    bool                            _active_session;

};

#endif /* CLIENT_HPP */

