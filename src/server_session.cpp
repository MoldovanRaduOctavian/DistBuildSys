#include "server_session.hpp"

#include <filesystem>
#include <fstream>

#include <boost/uuid/uuid_io.hpp>
#include <string>

#include "client_view.hpp"
#include "distbuild_messages.pb.h"
#include "file_state_view.hpp"
#include "proto_io.hpp"

boost::asio::awaitable<void> ServerSession::handle_session() {
    
    for (;;) {
        // receive and handle all types of packets from the client
        distbuild::ClientMessage client_message;
        co_await proto_io::receive_msg(_session_socket, client_message);
        switch (client_message.content_case()) {
            case distbuild::ClientMessage::kSessionStart:
                // Do nothing
                break;
            case distbuild::ClientMessage::kFileChunkUpload: {
                // Do we use co_await for this, or co_spawn ?
                auto file_chunk_msg = 
                    std::make_shared<distbuild::ClientFileChunkUploadRequest>
                        (
                        client_message.file_chunk_upload()    
                        );
                
                // I need to call this in such a way that multiple
                // files can be received over TCP, in parallel
                co_await handle_file_transfer(file_chunk_msg);
                /*
                boost::asio::co_spawn
                    (
                    _session_socket.get_executor(), 
                    handle_file_transfer(file_chunk_msg),
                    boost::asio::detached
                    );
                */
                break;

            }

            default:
                break;
        }
    }

}   /* ServerSession::handle_session() */


boost::asio::awaitable<void> ServerSession::handle_file_transfer
    ( 
    std::shared_ptr<distbuild::ClientFileChunkUploadRequest>
                file_chunk_msg
    )    
{
    auto set_file_state = 
        [this](
                const std::string &     client_path, 
                const FileStateView &   file_state,
                FileStateView::Status   new_status
                ) {
            FileStateView updated_file_state = file_state;
            updated_file_state.file_status = new_status;
            _parent_client_view->update_file_state
                (
                client_path, 
                updated_file_state
                );

        };

    // This coroutine should be really strict about checking 
    // the preconditions and postconditions for a file tranfer
    const FileStateView * curr_file_state = _parent_client_view->get_file_state(file_chunk_msg->filename());
    if (curr_file_state == nullptr
     || file_chunk_msg->session_id() != boost::uuids::to_string(get_session_uuid())
     || file_chunk_msg->offset() != 0 || file_chunk_msg->sequence_no() != 0 
        ) 
    {
        // handle error
        set_file_state
            (
            file_chunk_msg->filename(), 
            *curr_file_state,
            FileStateView::Status::FILE_STATUS_FAULT
            );

        distbuild::ServerMessage session_abort_msg;
        session_abort_msg.mutable_session_abort()->set_session_id
            (
            boost::uuids::to_string(get_session_uuid()) 
            );

        co_await proto_io::send_msg(_session_socket, session_abort_msg);
        co_return;

    }
            
    std::string server_file_name = curr_file_state->server_file_path;

    bool is_last_chunk = file_chunk_msg->is_last_chunk();
    uint64_t last_seq_no = file_chunk_msg->sequence_no();
    uint64_t last_offset = file_chunk_msg->offset();
    
    std::ofstream received_file_wr_stream(server_file_name, std::ios::binary);
    // I really need to rename the data field in the protobuf message
    received_file_wr_stream.write(file_chunk_msg->data().data(), file_chunk_msg->data().size());

    // We could also check if the filename is valid
    std::string last_filename = file_chunk_msg->filename();

    while (!is_last_chunk) {
        distbuild::ClientFileChunkUploadRequest next_file_chunk;
        co_await proto_io::receive_msg(_session_socket, next_file_chunk);        
        if (next_file_chunk.session_id() != boost::uuids::to_string(get_session_uuid())
         || next_file_chunk.sequence_no() != last_seq_no + 1
         || next_file_chunk.offset() <= last_offset
         || next_file_chunk.filename() != last_filename
        )
        {
            // handle error
            // FileStateView is small, no problem copying it
            set_file_state
                (
                file_chunk_msg->filename(), 
                *curr_file_state,
                FileStateView::Status::FILE_STATUS_FAULT
                );
            
            distbuild::ServerMessage session_abort_msg;
            session_abort_msg.mutable_session_abort()->set_session_id
                (
                boost::uuids::to_string(get_session_uuid()) 
                );

            co_await proto_io::send_msg(_session_socket, session_abort_msg);
            co_return;
        }



        received_file_wr_stream.write(next_file_chunk.data().data(), next_file_chunk.data().size());
        is_last_chunk = next_file_chunk.is_last_chunk();
        last_filename = next_file_chunk.filename();
        last_seq_no = next_file_chunk.sequence_no();
        last_offset = next_file_chunk.offset();

    }

    received_file_wr_stream.close();
    if (curr_file_state->file_sz_bytes != std::filesystem::file_size(server_file_name)) {
        // handle error
        // also delete the file?
        // update file state view status enum
        set_file_state
            (
            file_chunk_msg->filename(), 
            *curr_file_state,
            FileStateView::Status::FILE_STATUS_FAULT
            );
        
        distbuild::ServerMessage session_abort_msg;
        session_abort_msg.mutable_session_abort()->set_session_id
            (
            boost::uuids::to_string(get_session_uuid()) 
            );

        co_await proto_io::send_msg(_session_socket, session_abort_msg);
        co_return;

    }
    
    std::ifstream received_file_rd_stream(server_file_name);
    boost::uuids::detail::sha1::digest_type received_file_sha1{0};
    generate_file_sha1(received_file_rd_stream, received_file_sha1);
    
    std::string received_file_sha1_str = std::string
        (
        reinterpret_cast<const char *>(received_file_sha1),
        sizeof(received_file_sha1)
        );
    
    if (curr_file_state->file_sha1 != received_file_sha1_str) {
        // handle error
        set_file_state
            (
            file_chunk_msg->filename(), 
            *curr_file_state,
            FileStateView::Status::FILE_STATUS_FAULT
            );
        
        distbuild::ServerMessage session_abort_msg;
        session_abort_msg.mutable_session_abort()->set_session_id
            (
            boost::uuids::to_string(get_session_uuid()) 
            );

        co_await proto_io::send_msg(_session_socket, session_abort_msg);
        co_return;

    }    

    // This should also try to launch the compilation for all the other sessions?
    // Other sessions might be waiting for this file transfer
    
    // If none of the validity checks failed, then mark the file state as available
    set_file_state
        (
        file_chunk_msg->filename(),
        *curr_file_state,
        FileStateView::Status::FILE_STATUS_AVAILABLE
        );

    // What do I do if any chunk transmission fails ???
    distbuild::ServerMessage file_up_complete_msg;
    file_up_complete_msg.mutable_file_up_complete()->set_session_id
        (
        boost::uuids::to_string(get_session_uuid())
        );
    file_up_complete_msg.mutable_file_up_complete()->set_filename
        (
        file_chunk_msg->filename() 
        );
    file_up_complete_msg.mutable_file_up_complete()->set_success(true);
    co_await proto_io::send_msg(_session_socket, file_up_complete_msg);

}   /* ServerSession::handle_file_transfer() */

