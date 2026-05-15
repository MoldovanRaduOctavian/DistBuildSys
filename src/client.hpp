#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <memory>
#include <unordered_map>

#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <google/protobuf/message.h>

#include "client_session.hpp"
#include "compiler_call.hpp"
#include "includes_rework.hpp"
#include "unix_ipc_socket.hpp"

class Client {

public:

    Client
        (
        boost::asio::io_context & 
                            io_ctx
        ) :
        _io_ctx(io_ctx),
        _unix_ipc_manager(
            io_ctx, 
            "/tmp/distbuild_ipc.sock",
            [this](const UnixIpcRequest & ipc_request) -> UnixIpcResponse {
                return _handle_ipc_request(ipc_request);                
            })
        {
        // _client_uuid could be read out of a YAML
        // and generated if it does not already exist
        _system_include_dirs.find_system_includes("/usr/bin/clang");

        };

    boost::asio::awaitable<void> connect_to_server_v1(const char * req_file);
    
    boost::asio::awaitable<void> connect_to_server
        (
        const std::string & server_ip,
        uint16_t            server_port,
        CompilerCall &      compiler_call
        );
    
    void remove_client_session(const boost::uuids::uuid & session_uuid);

    // Do this for testing depfiles
    std::string test_handle_compiler_call(CompilerCall & compiler_call) {
        std::vector<IncludeInfo> src_dependencies = 
            compiler_call.collect_src_file_dependencies(_system_include_dirs);
        
        /*
        for (const IncludeInfo & inc_info : src_dependencies) {
            std::cout << inc_info.file_path << '\n';
        }
        */

        std::string dep_file_name;
        DepFilesState & dep_files_state = compiler_call.get_dep_files_state();
        if (dep_files_state.is_depfile_needed()) {
            std::vector<const IncludeInfo *> src_deps_ptrs;
            for (const IncludeInfo & inc_info : src_dependencies) {
                src_deps_ptrs.emplace_back(&inc_info);
            }

            dep_file_name = dep_files_state.generate_dep_file(
                compiler_call, 
                src_deps_ptrs
            );
        }
        
        return dep_file_name;

    }    
    
    CmdLineIncludeDirs & get_system_include_dirs() {
        return _system_include_dirs;
    }
    
    const boost::uuids::uuid & get_client_uuid() const {
        return _client_uuid;
    }

private:
    
    UnixIpcResponse _handle_ipc_request(const UnixIpcRequest & ipc_request);

    boost::uuids::uuid              _client_uuid;
    boost::asio::io_context &       _io_ctx;
    
    // All of these need to have thread safe interfaces
    // because ClientSessions working in parallel will be 
    // interacting with these things
    
    UnixIpcManager                  _unix_ipc_manager;

    CmdLineIncludeDirs              _system_include_dirs;

    std::unordered_map<std::string, std::unique_ptr<ClientSession>>
                                    _client_sessions;
    std::unordered_map<std::string, std::unique_ptr<CompilerCall>>
                                    _client_compiler_calls;
};

#endif /* CLIENT_HPP */

