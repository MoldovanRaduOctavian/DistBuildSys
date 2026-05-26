#ifndef RESOURCE_BROADCASTER_HPP
#define RESOURCE_BROADCASTER_HPP

#include <chrono>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "distbuild_messages.pb.h"

class ResourceBroadcaster {

public:
    ResourceBroadcaster
        (
        boost::asio::io_context & 
                    io_ctx,
        uint16_t    broadcast_port,
        
        uint16_t    server_port,
        const std::string &
                    server_ip
        ) :
        _io_ctx(io_ctx),
        _strand(boost::asio::make_strand(io_ctx)),
        _broadcast_port(broadcast_port),
        _server_port(server_port),
        _server_ip(server_ip),
        _udp_socket(_strand),
        _endpoint(
            boost::asio::ip::address_v4::broadcast(),
            broadcast_port
        ),
        _timer(_strand)
        {
            _udp_socket.open(boost::asio::ip::udp::v4());
            _udp_socket.set_option
                (
                boost::asio::socket_base::broadcast(true)
                );

            _start();

        };

private:
    
    void _start() {
        boost::asio::co_spawn(
            _strand,
            _run(),
            boost::asio::detached
        );
    }

    boost::asio::awaitable<void> _run()
    {
        for (;;) {
            
            // Send the advertisement packets
            distbuild::ResourceAdvMessage resource_msg;
            resource_msg.set_server_uuid("server_ip");
            resource_msg.set_server_host(_server_ip);
            resource_msg.set_server_port(_server_port);
            resource_msg.set_available_jobs(8);

            std::string payload;
            resource_msg.SerializeToString(&payload);

            co_await _udp_socket.async_send_to(
                boost::asio::buffer(payload),
                _endpoint,
                boost::asio::use_awaitable
            );

            _timer.expires_after
                (
                std::chrono::milliseconds(100)
                );
            co_await _timer.async_wait(boost::asio::use_awaitable);
        }
    }

    boost::asio::io_context & 
                _io_ctx;
    boost::asio::strand<boost::asio::any_io_executor>
                _strand;

    uint16_t    _broadcast_port;
    uint16_t    _server_port;
    std::string _server_ip;
    
    boost::asio::ip::udp::socket
                _udp_socket;
    boost::asio::ip::udp::endpoint
                _endpoint;
    boost::asio::steady_timer
                _timer;
 

};

#endif /* RESOURCE_BROADCASTER_HPP */
