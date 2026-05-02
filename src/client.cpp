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


boost::asio::awaitable<void> Client::connect_to_server
    (
    const std::string & server_ip,
    uint16_t            server_port,
    CompilerCall &      compiler_call
    )
{
    ClientSession client_session(_io_ctx, compiler_call);
    
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
        co_await client_session.start_client_session
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
    }

}   /* Client::connect_to_server() */

/*
boost::asio::awaitable<void> Client::connect_to_server_v1
    (
    const char * req_file
    ) 
{
    
    // This message exchange is meant to test the server capabilities
    std::cout << "COMPILED FILE: " << req_file << '\n';    
    distbuild::ClientMessage session_start_request; 
    session_start_request.mutable_session_start()
        ->set_client_id("3f2c9d1e-7b6a-4a5f-9c21-8d4e6a91c0b3");
    session_start_request.mutable_session_start()
        ->set_client_working_dir("/home/radu/distbuild/DistBuildSys");
    session_start_request.mutable_session_start()
        ->set_client_compiler("/usr/bin/g++");

    session_start_request.mutable_session_start()
        ->set_client_source_file(req_file);

    distbuild::FileInfo * src_file_info = session_start_request.mutable_session_start()
        ->add_required_files_info();
        
    src_file_info->set_filename(req_file);
    src_file_info->set_filesize
        (
        std::filesystem::file_size(req_file)
        );


    // Do I need to open this as binary or not???    
    std::ifstream src_file_stream(req_file);
    boost::uuids::detail::sha1::digest_type src_file_sha1{0};
    generate_file_sha1(src_file_stream, src_file_sha1);
    std::string src_file_sha1_str = std::string(
        reinterpret_cast<const char *>(src_file_sha1),
        sizeof(src_file_sha1)
        );
    
    src_file_info->set_filehash(src_file_sha1_str);

    boost::asio::ip::tcp::endpoint server_endpoint
        (
        boost::asio::ip::make_address(_server_ip),
        _server_port 
        );

    co_await _tcp_socket.async_connect(server_endpoint, boost::asio::use_awaitable);

    // Just send these for now

    // What do I add to cmd line args ???
    session_start_request.mutable_session_start()->add_client_cmd_line_args(
        "-std=c++17"
    );
    
    co_await proto_io::send_msg(_tcp_socket, session_start_request);
    
    distbuild::ServerMessage session_confirmation_msg;
    co_await proto_io::receive_msg(_tcp_socket, session_confirmation_msg);
    if (session_confirmation_msg.content_case() == distbuild::ServerMessage::kSessionConfirmed) {
    std::cout << session_confirmation_msg.session_confirmed().session_id() << '\n';
        std::cout << "THESE ARE THE REQUIRED FILES: \n";
        for (const std::string & required_file : 
            session_confirmation_msg.session_confirmed().required_files()) {     
            std::cout << required_file << '\n';
            

            std::ifstream requested_file_stream(required_file, std::ios::binary);  
            const size_t OBJ_CHUNK_SZ = 64 * 1024;
            std::vector<char> chunk_buff(OBJ_CHUNK_SZ);
            uint64_t seq_no = 1;
            uint64_t file_offset = 0;
            distbuild::ClientMessage requested_file_chunk_msg{};
            while (requested_file_stream.read(chunk_buff.data(), OBJ_CHUNK_SZ)
                    || requested_file_stream.gcount())
            {
                auto bytes_read = requested_file_stream.gcount();

                distbuild::ClientMessage msg;
                auto * chunk = msg.mutable_file_chunk_upload();

                chunk->set_session_id(session_confirmation_msg.session_confirmed().session_id());
                chunk->set_filename(required_file);
                chunk->set_sequence_no(seq_no++);
                chunk->set_offset(file_offset);
                chunk->set_data(chunk_buff.data(), bytes_read);
                chunk->set_is_last_chunk(bytes_read < OBJ_CHUNK_SZ);
                if (chunk->is_last_chunk()) {
                    std::cout << "LAST CHUNK WAS SET!\n";
                }

                file_offset += bytes_read;

                co_await proto_io::send_msg(_tcp_socket, msg);
            }

        }
        
        distbuild::ServerMessage all_req_uploaded_msg; 
        co_await proto_io::receive_msg(_tcp_socket, all_req_uploaded_msg);
        // I don't know what to do with this type of message


        distbuild::ServerMessage obj_file_msg;
        co_await proto_io::receive_msg(_tcp_socket, obj_file_msg);
        if (obj_file_msg.content_case() == distbuild::ServerMessage::kObjFileChunkTransmit) {
            std::cout << "Compiler exit code: " << 
                obj_file_msg.obj_file_chunk_transmit().compiler_exit_code() << '\n';
            std::cout << "Compiler stdout:" <<
                obj_file_msg.obj_file_chunk_transmit().compiler_stdout() << '\n';
            std::cout << "Compiler stderr:" <<
                obj_file_msg.obj_file_chunk_transmit().compiler_stderr() << '\n';
        }
        
        
    }

}   
*/

