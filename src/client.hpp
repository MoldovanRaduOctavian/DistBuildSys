#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <unordered_map>

#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <google/protobuf/message.h>

#include "client_session.hpp"
#include "compiler_call.hpp"
#include "includes_rework.hpp"

class Client {

public:

    Client
        (
        boost::asio::io_context & 
                            io_ctx
        ) :
        _io_ctx(io_ctx)
        {
        // _client_uuid could be read out of a YAML
        // and generated if it does not already exist
        _system_include_dirs.find_system_includes("/usr/bin/clang++");

        };

    boost::asio::awaitable<void> connect_to_server_v1(const char * req_file);
    
    boost::asio::awaitable<void> connect_to_server
        (
        const std::string & server_ip,
        uint16_t            server_port,
        CompilerCall &      compiler_call
        );
    
private:
    
    boost::uuids::uuid              _client_uuid;
    boost::asio::io_context &       _io_ctx;
    
    // All of these need to have thread safe interfaces
    // because ClientSessions working in parallel will be 
    // interacting with these things

    CmdLineIncludeDirs              _system_include_dirs;

    std::unordered_map<std::string, ClientSession>
                                    _client_sessions;

};

#endif /* CLIENT_HPP */

