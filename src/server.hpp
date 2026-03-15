#ifndef SERVER_HPP
#define SERVER_HPP

#include <iostream>

#include <boost/asio.hpp>
#include <google/protobuf/message.h>

#include "filetransfer_messages.pb.h"

#include "proto_io.hpp"

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
        _tcp_socket(io_ctx),
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

    boost::asio::awaitable<void> listen_to_connections() {
        for (;;) {
            boost::asio::ip::tcp::socket tcp_socket = 
                co_await _tcp_acceptor.async_accept(boost::asio::use_awaitable);
            boost::asio::co_spawn(_io_ctx, handle_connection(std::move(tcp_socket)), boost::asio::detached);            
        }
        
    } 

    boost::asio::awaitable<void> handle_connection
        (
        boost::asio::ip::tcp::socket tcp_socket    
        )
    {  
        filetransfer::ClientMessage client_message;
        co_await proto_io::receive_msg(tcp_socket, client_message);
        switch (client_message.content_case()) {
            case filetransfer::ClientMessage::kFileRequest:
                std::cout   << "THIS IS WHAT THE CLIENT SENT US: \n"
                            << client_message.file_request().filename() << '\n'
                            << client_message.file_request().filesize() << '\n';
                break;
            default:
                std::cout << "Unknown protobuf message\n";
                break;                
        }

        filetransfer::ServerMessage server_message;
        server_message.mutable_upload_status()->set_filename("MESSAGE FOR THE CLIENT");
        co_await proto_io::send_msg(tcp_socket, server_message);
        
        boost::system::error_code ec;
        auto ec1 = tcp_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        co_await boost::asio::post(tcp_socket.get_executor(), boost::asio::use_awaitable);
        tcp_socket.close();

    }

private:

    boost::asio::io_context &       _io_ctx;
    // Use this to accept 1 connection, for now
    boost::asio::ip::tcp::socket    _tcp_socket;
    boost::asio::ip::tcp::acceptor  _tcp_acceptor;
    std::string                     _server_ip;
    uint16_t                        _server_port;

};

#endif /* SERVER_HPP */

