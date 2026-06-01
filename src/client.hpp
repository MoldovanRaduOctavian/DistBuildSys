#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <memory>
#include <unordered_map>

#include <boost/asio.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/ip/address.hpp>
#include <google/protobuf/message.h>

#include "client_session.hpp"
#include "compiler_call.hpp"
#include "includes_rework.hpp"
#include "unix_ipc_socket.hpp"
#include "resource_listener.hpp"

class Client {

public:

    Client
        (
        boost::asio::io_context & 
                            io_ctx,
        const boost::uuids::uuid &
                            client_uuid,
        uint16_t            advertising_port,
        int                 worker_pool_sz
        ) :
        _io_ctx(io_ctx),
        _client_uuid(client_uuid),
        _unix_ipc_manager(
            io_ctx, 
            "/tmp/distbuild_ipc.sock",
            worker_pool_sz,
            [this](const UnixIpcRequest & ipc_request) -> boost::asio::awaitable<UnixIpcResponse> {
                co_return co_await _handle_ipc_request(ipc_request);                
            }),
        _resource_listener(io_ctx, advertising_port),
        _includes_cache(_system_include_dirs)
        {
        // _client_uuid could be read out of a YAML
        // and generated if it does not already exist
        _unix_ipc_manager.start_ipc_manager();
        };

    boost::asio::awaitable<void> connect_to_server_v1(const char * req_file);
    
    boost::asio::awaitable<ClientSession *> connect_to_server
        (
        const std::string & server_ip,
        uint16_t            server_port,
        std::unique_ptr<CompilerCall>
                            compiler_call
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
    
    boost::asio::awaitable<UnixIpcResponse> _handle_ipc_request(const UnixIpcRequest & ipc_request);
    
    boost::asio::io_context &       _io_ctx;
    boost::uuids::uuid              _client_uuid;
    
    // All of these need to have thread safe interfaces
    // because ClientSessions working in parallel will be 
    // interacting with these things
    
    UnixIpcManager                  _unix_ipc_manager;
    
    ResourceListener                _resource_listener;

    CmdLineIncludeDirs              _system_include_dirs;
    
    IncludesCache                   _includes_cache;

    std::unordered_map<std::string, std::unique_ptr<ClientSession>>
                                    _client_sessions;
    std::unordered_map<std::string, std::unique_ptr<CompilerCall>>
                                    _client_compiler_calls;

    mutable std::mutex              _mtx;
};

#endif /* CLIENT_HPP */

