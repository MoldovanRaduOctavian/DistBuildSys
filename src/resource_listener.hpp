#ifndef RESOURCE_LISTENER_HPP
#define RESOURCE_LISTENER_HPP

#include <array>
#include <chrono>
#include <optional>
#include <unordered_map>

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include "distbuild_messages.pb.h"

class ResourceListener {

public:
    
    struct ServerInfo {
        std::string server_uuid;
        std::string server_ip;
        uint32_t    server_port;
        uint32_t    available_jobs;
        std::chrono::steady_clock::time_point
                    last_seen;
    };

    ResourceListener
        (
        boost::asio::io_context &
                    io_ctx,
        uint16_t    listener_port
        ) :
        _io_ctx(io_ctx),
        _strand(boost::asio::make_strand(_io_ctx)),
        _listener_port(listener_port),
        _udp_socket(
            _strand,
            boost::asio::ip::udp::endpoint(
                boost::asio::ip::udp::v4(),
                listener_port
            )
        ),
        _cleanup_timer(_strand)
        {
            _start();
        };
    
    std::optional<ResourceListener::ServerInfo> pick_compilation_server() {
        std::lock_guard<std::mutex> lock(_mtx);
        for (auto it=_server_nodes.begin(); it != _server_nodes.end(); it++) {
            if (it->second.available_jobs > 0) {
                it->second.available_jobs--;
                return it->second;
            }
        }

        return std::nullopt;

    }

private:
    
    void _start() {
        boost::asio::co_spawn(
            _strand,
            _receive_loop(),
            boost::asio::detached
        ); 

        boost::asio::co_spawn(
            _strand,
            _cleanup_loop(),
            boost::asio::detached
        ); 

    }
    
    boost::asio::awaitable<void> _receive_loop() {
        
        std::array<char, 2048> buffer;

        for (;;) {
            boost::asio::ip::udp::endpoint sender_endpoint;
            auto bytes = 
                co_await _udp_socket.async_receive_from(
                    boost::asio::buffer(buffer),
                    sender_endpoint,
                    boost::asio::use_awaitable
                );

            distbuild::ResourceAdvMessage resource_msg;
            if (!resource_msg.ParseFromArray(
                buffer.data(),
                static_cast<int>(bytes)
            )) {
                continue;
            }
            
            std::lock_guard<std::mutex> lock(_mtx);
            ServerInfo server_info;
            server_info.server_uuid = resource_msg.server_uuid();
            server_info.server_ip = resource_msg.server_host();
            server_info.server_port = resource_msg.server_port();
            server_info.available_jobs = resource_msg.available_jobs();
            server_info.last_seen = std::chrono::steady_clock::now();
            
            _server_nodes[server_info.server_uuid] = server_info;
#if 0
            std::cout << _server_nodes.at(server_info.server_uuid).server_ip
                    << " : " << _server_nodes.at(server_info.server_uuid).server_port
                    << '\n';
#endif
        }

    }

    boost::asio::awaitable<void> _cleanup_loop() {
        for (;;) {
            _cleanup_timer.expires_after(std::chrono::seconds(2));
            co_await _cleanup_timer.async_wait(boost::asio::use_awaitable);

            auto now = std::chrono::steady_clock::now();
            
            std::lock_guard<std::mutex> lock(_mtx);
            for (auto it = _server_nodes.begin(); it != _server_nodes.end(); ) {
                auto info_age = std::chrono::duration_cast<std::chrono::seconds>(
                    now - it->second.last_seen
                );
                
                // This might be extremely broken
                if (info_age.count() > 4) {
                    std::cout << "CLEANED UP: " << it->second.server_uuid
                            << ", " << it->second.server_ip << '\n';
                    it = _server_nodes.erase(it);
                }
                else {
                    it++;
                }
            }
        }
    }

    boost::asio::io_context &
                _io_ctx;
    boost::asio::strand<boost::asio::any_io_executor>
                _strand;

    uint16_t    _listener_port;
    
    boost::asio::ip::udp::socket
                _udp_socket;
    boost::asio::steady_timer
                _cleanup_timer;

    mutable std::mutex _mtx;
    std::unordered_map<std::string, ServerInfo>
                _server_nodes;


};

#endif /* RESOURCE_LISTENER_HPP */
