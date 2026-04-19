#include "client.hpp"

#include <fstream>
#include <filesystem>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/uuid/detail/sha1.hpp>
#include <google/protobuf/message.h>

#include "distbuild_messages.pb.h"
#include "file_state_view.hpp"
#include "proto_io.hpp"

boost::asio::awaitable<void> Client::connect_to_server() {
    
    // This message exchange is meant to test the server capabilities
    
    distbuild::ClientMessage session_start_request; 
    session_start_request.mutable_session_start()
        ->set_client_id("3f2c9d1e-7b6a-4a5f-9c21-8d4e6a91c0b3");
    session_start_request.mutable_session_start()
        ->set_client_working_dir("/home/radu/distbuild/DistBuildSys");
    session_start_request.mutable_session_start()
        ->set_client_compiler("/usr/bin/clang++");
    // session_start_request.mutable_session_start()
    //    ->set_client_source_file("/home/radu/distbuild/DistBuildSys/src/client.cpp"); 
    // session_start_request.mutable_session_start()
    //    ->set_client_source_file("/home/radu/distbuild/DistBuildSys/src/test_src.cpp");
    session_start_request.mutable_session_start()
        ->set_client_source_file("/home/radu/distbuild/DistBuildSys/src/distbuild_messages.pb.cc");
    
    distbuild::FileInfo * src_file_info = session_start_request.mutable_session_start()
        ->add_required_files_info();
    
    src_file_info->set_filename("/home/radu/distbuild/DistBuildSys/src/distbuild_messages.pb.cc");
    src_file_info->set_filesize
        (
        std::filesystem::file_size("/home/radu/distbuild/DistBuildSys/src/distbuild_messages.pb.cc")
        );

    // Do I need to open this as binary or not???
    
    std::ifstream src_file_stream("/home/radu/distbuild/DistBuildSys/src/distbuild_messages.pb.cc");
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
    // session_start_request.mutable_session_start()
    //      ->set_client_cmd_line_args()
    
    co_await proto_io::send_msg(_tcp_socket, session_start_request);
    
    std::cout << "CLIENT MESSAGE WAS SENT!!!\n";

    distbuild::ServerMessage session_confirmation_msg;
    co_await proto_io::receive_msg(_tcp_socket, session_confirmation_msg);
    if (session_confirmation_msg.content_case() == distbuild::ServerMessage::kSessionConfirmed) {
        std::cout << session_confirmation_msg.session_confirmed().session_id() << '\n';
        std::cout << "THESE ARE THE REQUIRED FILES: \n";
        for (const std::string & required_file : 
            session_confirmation_msg.session_confirmed().required_files()) {     
            std::cout << required_file << '\n';
            

            std::ifstream requested_file_stream(required_file, std::ios::binary);  
            const size_t OBJ_CHUNK_SZ = 32 * 1024;
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
        
        
    }

}   /* Client::connect_to_server() */

