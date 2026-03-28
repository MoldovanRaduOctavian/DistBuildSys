#include "client.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <google/protobuf/message.h>


boost::asio::awaitable<void> Client::connect_to_server() {
    /*
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
    */
    co_return;

}   /* Client::connect_to_server() */

