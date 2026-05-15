#include "client.hpp"

#include <fstream>
#include <filesystem>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/uuid/detail/sha1.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <google/protobuf/message.h>

#include "client_session.hpp"
#include "distbuild_messages.pb.h"
#include "file_state_view.hpp"

// A connection to one of the servers
// You can have multiple connections to the same server
boost::asio::awaitable<void> Client::connect_to_server
    (
    const std::string & server_ip,
    uint16_t            server_port,
    CompilerCall &      compiler_call
    )
{
    // ClientSession client_session(_io_ctx, compiler_call);
    auto client_session = std::make_unique<ClientSession>(_io_ctx, *this, compiler_call);
    
    distbuild::ClientMessage client_message;
    auto * client_session_start_rqst = 
        client_message.mutable_session_start();

    client_session_start_rqst->set_client_id
        (
        boost::uuids::to_string(_client_uuid)
        );

    // I think that all paths should be converted to absolute paths
    // TODO ENFORCE THIS !!!
    client_session_start_rqst->set_client_working_dir
        (
        compiler_call.get_current_working_dir()
        );

    client_session_start_rqst->set_client_compiler
        (
        compiler_call.get_compiler_type()
        );

    std::string src_file_path = compiler_call.get_input_src_file();
    client_session_start_rqst->set_client_source_file
        (
        src_file_path
        );
    
    const std::vector<std::string> & cmd_line_args = 
        compiler_call.get_cmd_line_args();
    for (const std::string & arg : cmd_line_args) {
        client_session_start_rqst->add_client_cmd_line_args
            (
            arg 
            );
    }

    std::vector<std::string> idirs_args =
        compiler_call.get_cmd_line_include_dirs().cnvt_to_cmd_line_args();
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
        compiler_call.collect_src_file_dependencies(_system_include_dirs);
    for (const IncludeInfo & include_info : src_file_dependencies) {
        distbuild::FileInfo * file_info = 
            client_session_start_rqst->add_required_files_info();
        file_info->set_filename(include_info.file_path);
        file_info->set_filehash(include_info.include_hash);
        file_info->set_filesize(include_info.include_sz_bytes);
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
        _client_sessions.try_emplace
            (
            boost::uuids::to_string(session_uuid),
            std::move(client_session) 
            );
    }
    else {
        // ... We need to invalidate the session somehow
        client_session->perform_local_compilation();
        client_session->terminate_client_session();
        // Send the results back to the wrapper
        // Perform a session cleanup

    }

}   /* Client::connect_to_server() */


void Client::remove_client_session
    (
    const boost::uuids::uuid & session_uuid
    )
{
    const std::string session_uuid_str = boost::uuids::to_string(session_uuid);
    if (_client_sessions.find(session_uuid_str) != _client_sessions.end()) {
        _client_sessions.erase(session_uuid_str);
    }
    
    // This is where the compiler call also gets removed

}   /* Client::remove_client_session() */


UnixIpcResponse Client::_handle_ipc_request
    (
    const UnixIpcRequest & ipc_request
    )
{
    // This should be non-blocking
    // This is not alright

}   /* Client::_handle_ipc_request() */

