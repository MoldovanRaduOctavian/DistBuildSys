#include "server.hpp"

#include <iostream>

#include <boost/asio.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <google/protobuf/message.h>

#include "distbuild_messages.pb.h"

#include "proto_io.hpp"


boost::asio::awaitable<void> Server::listen_for_connections() {

    for (;;) {
        // This will be updated to accomodate multiple connections soon
        
        boost::asio::ip::tcp::socket tcp_socket = 
            co_await _tcp_acceptor.async_accept(boost::asio::use_awaitable);
         
        std::cout << "DO WE EVEN GET HERE ANYMORE?\n";


        // This is where we create a new session
        distbuild::ClientMessage client_message;
        co_await proto_io::receive_msg(tcp_socket, client_message);
        if (client_message.content_case() == distbuild::ClientMessage::kSessionStart) {
            // Handle a new session request
            // A client can have multiple compilation sessions allocated to him
            ClientView * session_client_view = 
                _client_views.add_client_view(client_message.session_start().client_id());
            
            // This does much more than simply adding a new session
            // It initializes the session and performs a lot of bookkeeping
            session_client_view->add_session
                (
                std::move(tcp_socket), 
                client_message.session_start(),
                &_compiler_manager
                );
             
        }
        else {
            // Close the socket immediately, send an error message maybe?
            boost::system::error_code ec;
            auto ec1 = tcp_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            co_await boost::asio::post(tcp_socket.get_executor(), boost::asio::use_awaitable);
            tcp_socket.close();
        }

    }
    
}   /* Server::listen_to_connections() */

