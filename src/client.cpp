#include "client.hpp"

#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <memory>

#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/process.hpp>
#include <boost/process/detail/child_decl.hpp>
#include <boost/uuid/detail/sha1.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <google/protobuf/message.h>

#include "client_session.hpp"
#include "distbuild_messages.pb.h"
#include "file_state_view.hpp"

// A connection to one of the servers
// You can have multiple connections to the same server
boost::asio::awaitable<ClientSession *> Client::connect_to_server
    (
    const std::string & server_ip,
    uint16_t            server_port,
    std::unique_ptr<CompilerCall>
                        compiler_call
    )
{   
    // This deallocates when the function ends ....
    auto client_session = std::make_unique<ClientSession>(_io_ctx, *this, *compiler_call);
    distbuild::ClientMessage client_message;
    auto * client_session_start_rqst = 
        client_message.mutable_session_start();

    {
        std::lock_guard<std::mutex> lock(_mtx); 
        client_session_start_rqst->set_client_id
            (
            boost::uuids::to_string(_client_uuid)
            );

        // I think that all paths should be converted to absolute paths
        // TODO ENFORCE THIS !!!
        client_session_start_rqst->set_client_working_dir
            (
            compiler_call->get_current_working_dir()
            );

        client_session_start_rqst->set_client_compiler
            (
            compiler_call->get_compiler_type()
            );

        std::string src_file_path = compiler_call->get_input_src_file();
        client_session_start_rqst->set_client_source_file
            (
            src_file_path
            );
        
        const std::vector<std::string> & cmd_line_args = 
            compiler_call->get_cmd_line_args();
        for (const std::string & arg : cmd_line_args) {
            client_session_start_rqst->add_client_cmd_line_args
                (
                arg 
                );
            // std::cout << arg << '\n';
        }

        std::vector<std::string> idirs_args =
            compiler_call->get_cmd_line_include_dirs().cnvt_to_cmd_line_args();
        for (const std::string & idir : idirs_args) {
            client_session_start_rqst->add_client_idirs(idir);
        }
            
        std::ifstream src_file_stream(src_file_path);
        boost::uuids::detail::sha1::digest_type src_file_sha1{0};
        generate_file_sha1(src_file_stream, src_file_sha1);
        std::string src_file_sha1_str = std::string(
            reinterpret_cast<const char *>(src_file_sha1),
            sizeof(src_file_sha1)
        );
        
        uint64_t src_file_size = 
            std::filesystem::file_size(src_file_path);

        distbuild::FileInfo * src_file_info =
            client_session_start_rqst->add_required_files_info();

        src_file_info->set_filename(src_file_path);
        src_file_info->set_filehash(src_file_sha1_str);
        src_file_info->set_filesize(src_file_size);

        std::vector<IncludeInfo> src_file_dependencies =
            compiler_call->collect_src_file_dependencies(_system_include_dirs);
        for (const IncludeInfo & include_info : src_file_dependencies) {
            distbuild::FileInfo * file_info = 
                client_session_start_rqst->add_required_files_info();
            file_info->set_filename(include_info.file_path);
            file_info->set_filehash(include_info.include_hash);
            file_info->set_filesize(include_info.include_sz_bytes);
        }
    
    }

    boost::uuids::uuid session_uuid{0};
    bool session_creation_success = 
        co_await client_session->start_client_session
            (
            server_ip,
            server_port,
            client_message,
            session_uuid
            );

    if (session_creation_success == true) {
        std::lock_guard<std::mutex> lock(_mtx);
        _client_sessions.try_emplace
            (
            boost::uuids::to_string(session_uuid),
            std::move(client_session) 
            );
        _client_compiler_calls.try_emplace
            (
            boost::uuids::to_string(session_uuid),
            std::move(compiler_call)
            );

        std::cout << "INSIDE Client::connecto_to_server !!!\n";
        co_return _client_sessions[boost::uuids::to_string(session_uuid)].get();
    }
    else {
        // THIS IS BROKEN THIS NEEDS TO BE FIXED!!!
        // ... We need to invalidate the session somehow
        client_session->send_abort_to_server();
        co_await client_session->terminate_client_session();
        // Send the results back to the wrapper
        // Perform a session cleanup
        co_await client_session->perform_local_compilation();
        std::lock_guard<std::mutex> lock(_mtx);
        _client_sessions.try_emplace
            (
            boost::uuids::to_string(session_uuid),
            std::move(client_session) 
            );
        _client_compiler_calls.try_emplace
            (
            boost::uuids::to_string(session_uuid),
            std::move(compiler_call)
            ); 
        co_return _client_sessions[boost::uuids::to_string(session_uuid)].get();

    }
    
    // When you move a unique ptr, the unique_ptr.get() gets 
    // set to nullptr
 
}   /* Client::connect_to_server() */


void Client::remove_client_session
    (
    const boost::uuids::uuid & session_uuid
    )
{
    std::lock_guard<std::mutex> lock(_mtx);
    const std::string session_uuid_str = boost::uuids::to_string(session_uuid);
    if (_client_sessions.find(session_uuid_str) != _client_sessions.end()) {
        _client_sessions.erase(session_uuid_str);
    }
    
    if (_client_compiler_calls.find(session_uuid_str) != _client_compiler_calls.end()) {
        _client_compiler_calls.erase(session_uuid_str);
    }

}   /* Client::remove_client_session() */


// This should execute concurrently, so code in this
// function can be "blocking"
boost::asio::awaitable<UnixIpcResponse> Client::_handle_ipc_request
    (
    const UnixIpcRequest & ipc_request
    )
{
    auto compiler_call = std::make_unique<CompilerCall>
        (
        _includes_cache
        );
    
    compiler_call->initialize_compiler_call
        (
        ipc_request.current_working_dir, 
        ipc_request.cmd_line_args
        );
       
    CompilerCall::CallType compiler_call_type = compiler_call->get_compiler_call_type();
    if (compiler_call_type == CompilerCall::CallType::CALL_TYPE_COMPILE) { 
        std::cout << "WE ARE INSIDE _handle_ipc_request\n";
        ResourceListener::ServerInfo server_node;
        boost::asio::steady_timer find_sv_retry_timer(_io_ctx);
        for (;;) {
            auto server_info_opt = _resource_listener.pick_compilation_server();
            if (server_info_opt) {
                server_node = server_info_opt.value();
                std::cout << "SERVER STATS: " << server_node.available_jobs << '\n';
                break;
            }
            else {
                find_sv_retry_timer.expires_after(std::chrono::milliseconds(500));
                co_await find_sv_retry_timer.async_wait(boost::asio::use_awaitable);
            }
        }

        ClientSession * client_session = 
            co_await connect_to_server
                (
                server_node.server_ip, 
                server_node.server_port, 
                std::move(compiler_call)
                );
        
        // We do not arrive to this point when we request 
        // the same file to be compiled multiple times in parallel
        std::cout << "BEFORE retrieve_unix_ipc_response()!!!\n";
        UnixIpcResponse unix_ipc_response = 
            co_await client_session->retrieve_unix_ipc_response();
        

        std::cout << "THIS IS AFTER retrieve_unix_ipc_response()!!!\n";
        // The compilation session has finished
        // So we should dispose of it
        client_session->send_abort_to_server();
        co_await client_session->terminate_client_session();
        co_return unix_ipc_response;
        
    }
    else {
        /* If the call type is linking or some other kind, then we only compile locally */
        boost::process::ipstream stdout_stream;
        boost::process::ipstream stderr_stream;
        
        boost::process::child compiler_process
            (
            compiler_call->get_compiler_type(),
            boost::process::args(compiler_call->get_cmd_line_args()),
            boost::process::std_out > stdout_stream,
            boost::process::std_err > stderr_stream,
            boost::process::start_dir = compiler_call->get_current_working_dir() 
            );
         
        compiler_process.wait();

        std::ostringstream stdout_oss;
        stdout_oss << stdout_stream.rdbuf();
        
        std::ostringstream stderr_oss;
        stderr_oss << stderr_stream.rdbuf(); 

        auto compilation_end_ts = std::chrono::steady_clock::now();
        auto compilation_duration = std::chrono::duration_cast<std::chrono::seconds>
            (
            compilation_end_ts - compiler_call->get_call_creation_time()
            );

        int compiler_exit_code = compiler_process.exit_code();
        
        const std::string stdout_str = stdout_oss.str();
        const std::string stderr_str = stderr_oss.str();

        compiler_call->set_call_duration(compilation_duration);
        compiler_call->set_exit_code(compiler_exit_code);
        compiler_call->set_stdout_content(stdout_str);
        compiler_call->set_stderr_content(stderr_str);
        
        co_return UnixIpcResponse{
            compiler_exit_code,
            stdout_str,
            stderr_str
        };
    }
    
}   /* Client::_handle_ipc_request() */

