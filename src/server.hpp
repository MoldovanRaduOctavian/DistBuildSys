#ifndef SERVER_HPP
#define SERVER_HPP

#include <boost/asio.hpp>
#include <boost/asio/detached.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <google/protobuf/message.h>

#include "compiler_manager.hpp"
#include "client_views_storage.hpp"
#include "resource_broadcaster.hpp"
#include "server_session.hpp"

class Server {

public:
    
    Server
        (
        boost::asio::io_context &
                            io_ctx,
        const boost::uuids::uuid &
                            server_uuid,
        const std::string & server_ip,
        uint16_t            server_port
        ) :
        _io_ctx(io_ctx),
        _tcp_acceptor
            (
            io_ctx
            ),
        // This should not be hardcoded
        // It will be pulled from the YAML cfg
        _compiler_manager(8),
        _resource_broadcaster
            (
            io_ctx,
            // The broadcast port should be pulled from the YAML cfg
            10458,
            server_uuid,
            server_port,
            server_ip
            ),
        _server_uuid(server_uuid),
        _server_ip(server_ip),
        _server_port(server_port)
    {};
    
    void start_server() {
        boost::asio::ip::tcp::endpoint endpoint
            (
            boost::asio::ip::make_address(_server_ip),
            _server_port
            );
        
        _tcp_acceptor.open(endpoint.protocol());
        _tcp_acceptor.set_option(boost::asio::socket_base::reuse_address(true));
        _tcp_acceptor.bind(endpoint);
        _tcp_acceptor.listen();

        boost::asio::co_spawn(_io_ctx, listen_for_connections(), boost::asio::detached);
        _resource_broadcaster.start();
    }

    boost::asio::awaitable<void> listen_for_connections(); 
    
    void set_advertiser_available_jobs(size_t available_jobs) {
        _resource_broadcaster.set_available_jobs(available_jobs);
    }

private:

    boost::asio::io_context &       _io_ctx;
    boost::asio::ip::tcp::acceptor  _tcp_acceptor;
    
    /* The server owns the client views storage */
    ClientViewsStorage              _client_views;  
    CompilerManager                 _compiler_manager;
    ResourceBroadcaster             _resource_broadcaster;
    boost::uuids::uuid              _server_uuid;
    std::string                     _server_ip;
    uint16_t                        _server_port;

};

#endif /* SERVER_HPP */

