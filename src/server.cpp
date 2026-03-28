#include "server.hpp"

#include <boost/asio.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <google/protobuf/message.h>

#include "distbuild_messages.pb.h"

#include "proto_io.hpp"
#include "server_session.hpp"

boost::asio::awaitable<void> Server::listen_for_connections() {

    for (;;) {
        // This will be updated to accomodate multiple connections soon
        boost::asio::ip::tcp::socket tcp_socket = 
            co_await _tcp_acceptor.async_accept(boost::asio::use_awaitable);

        // This is where we create a new session
        distbuild::ClientMessage client_message;
        co_await proto_io::receive_msg(tcp_socket, client_message);
        if (client_message.content_case() == distbuild::ClientMessage::kSessionStart) {
            // Handle a new session request
            // A client can have multiple compilation sessions allocated to him
            ClientView * session_client_view = 
                _client_views.add_client_view(client_message.session_start().client_id());
            
            const auto & socket_executor = tcp_socket.get_executor();
            co_await session_client_view->add_session(std::move(tcp_socket), client_message.session_start());
             
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

/*
boost::asio::awaitable<void> Server::handle_session
    (
    ServerSession & server_session
    )
{
    // Handle client messages until the session is over
    for (;;) {
        distbuild::ClientMessage client_message;
        co_await proto_io::receive_msg(server_session.get_session_socket(), client_message);
        switch (client_message.content_case()) {
            case distbuild::ClientMessage::kSessionStart:
                // This is already handled, so receiving it again
                // should result in an error, maybe
                break;
            case distbuild::ClientMessage::kSessionAbort:
                // The client encountered an error state during the session
                // and requests the session to be terminated
                break;
            case distbuild::ClientMessage::kFileChunkUpload:
                // Call function to assemble file chunks 
                // Most likely this should not even be a coroutine
                // since we already read all the necessary data
                (void)handle_client_file_chunk(server_session, client_message.file_chunk_upload());
                break;
            default:
                break;
        }
    }

}


boost::asio::awaitable<void> Server::handle_client_file_chunk
    (
    ServerSession & server_session,
    const distbuild::ClientFileChunkUploadRequest & 
                    file_chunk_message
    )
{
    // Have a handle to a filestream for the currently received file
    // This is the full path of the file on the client node
    // The server should have access to a file state data structure
    file_chunk_message.filename();
    co_return;

}   
*/

