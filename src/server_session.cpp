#include "server_session.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include <boost/uuid/uuid_io.hpp>

#include "client_view.hpp"
#include "compiler_manager.hpp"
#include "distbuild_messages.pb.h"
#include "file_state_view.hpp"
#include "proto_io.hpp"

void ServerSession::preprocess_compiler_call
    (
    const std::string &     client_working_dir,             
    const std::vector<std::string> & 
                            compiler_cmd_line_args,
    const std::vector<std::string> &
                            compiler_idirs
    )
{
    auto handle_ffile_prefix_map = [this](const std::string & cmd_line_arg,
                                          const std::string & added_path)
    {
        if (!cmd_line_arg.starts_with("-ffile-prefix-map")) {
                return cmd_line_arg;
        }

        size_t split_idx = cmd_line_arg.find("=");
        std::string ffile_prefix_part = cmd_line_arg.substr(0, split_idx);
        std::string path_part = cmd_line_arg.substr(split_idx + 1);
        if (path_part.starts_with("/")) {
            path_part = added_path + "/" + path_part;
            return ffile_prefix_part + "=" + path_part; 
        }
        
        return cmd_line_arg;
    };

    std::string fixed_in_src_file = 
        _in_src_file.starts_with("/") ? cnvt_client_to_server_path(_current_working_dir, _in_src_file)
                                      : _in_src_file;

    // The name is really weird and it already confuses me
    // This might be really wrong, I got to be careful with paths
    // _current_working_dir = cnvt_client_to_server_path(_current_working_dir, client_working_dir);    
    
    std::lock_guard<std::mutex> lock(_mtx);
    _cmd_line_args.clear();
    for (size_t idir_idx = 0; idir_idx < compiler_idirs.size(); idir_idx += 2) {
        _cmd_line_args.emplace_back(compiler_cmd_line_args[idir_idx]);
        _cmd_line_args.emplace_back
            (
            cnvt_client_to_server_path(_current_working_dir, compiler_idirs[idir_idx + 1])    
            );
    }

    for (const std::string & cmd_line_arg : compiler_cmd_line_args) {
        _cmd_line_args.emplace_back
            ( 
            handle_ffile_prefix_map(cmd_line_arg, _current_working_dir)
            );
    }

    _cmd_line_args.insert(_cmd_line_args.end(), {"-o", _out_obj_file, _in_src_file});

}   /* ServerSession::preprocess_compiler_call() */


void ServerSession::try_request_src_compilation() {
    
    // Be really careful about staying thread safe while accessing file data 
    std::lock_guard<std::mutex> lock(_mtx);
    for (const FileStateView * required_file : _required_files_state) {
        if (required_file->file_status != FileStateView::Status::FILE_STATUS_AVAILABLE) {
            // Not all files necessary for compilation have been uploaded
            return;
        }
    }
    
    if (_is_compiler_running == false) {
        // Launch a compiler call thread
        // The compiler manager has a thread pool
        _compiler_manager->add_compilation_request(CompilationRequest{
            this, _compiler_name, _cmd_line_args, _current_working_dir
        });

        _is_compiler_running = true;
    }

}   /* ServerSession::request_src_compilation() */


void ServerSession::publish_compilation_results
    (
    const CompilationOutput & compilation_out
    )
{   
    {
    std::lock_guard<std::mutex> lock(_mtx);
    _compiler_exit_code = compilation_out.exit_code;
    _compiler_stdout = compilation_out.stdout_data;
    _compiler_stderr = compilation_out.stderr_data;
    _compile_duration = compilation_out.compiling_duration;
    }

    // We should now be able to send a the compiled object
    // file to the client
    // This is where we send out the object file by chunks 

}   /* ServerSession::publish_compilation_results() */


boost::asio::awaitable<void> ServerSession::send_compilation_result() {
    
    // What if the session socket gets used simulteneously form multiple threads???
    // That seems like a dangerous situation
    std::lock_guard<std::mutex> lock(_mtx);
    if (_compiler_exit_code != 0) {
        // Handle the case in which compilation has failed
        distbuild::ServerMessage fail_obj_file_response{};
        fail_obj_file_response.mutable_obj_file_chunk_transmit()
            ->set_session_id(boost::uuids::to_string(_session_uuid));
        fail_obj_file_response.mutable_obj_file_chunk_transmit()
            ->set_compiler_exit_code(_compiler_exit_code);
        fail_obj_file_response.mutable_obj_file_chunk_transmit()
            ->set_compiler_call_duration(_compile_duration);
        fail_obj_file_response.mutable_obj_file_chunk_transmit()
            ->set_compiler_stdout(_compiler_stdout);
        fail_obj_file_response.mutable_obj_file_chunk_transmit()
            ->set_compiler_stderr(_compiler_stderr);
        fail_obj_file_response.mutable_obj_file_chunk_transmit()
            ->set_filesize(0);
        fail_obj_file_response.mutable_obj_file_chunk_transmit()
            ->set_is_last_chunk(true);
        
        co_await proto_io::send_msg(_session_socket, fail_obj_file_response);
    }
    
    else {
        // Send out the object file in chunks...
    }

}   /* ServerSession::send_compilation_result() */


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
                
                // This is very shady, as I want to be able to receive
                // multiple files at the same time
                
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
                ) 
        {
            FileStateView updated_file_state = file_state;
            updated_file_state.file_status = new_status;
            _parent_client_view->update_file_state
                (
                client_path, 
                updated_file_state
                );

        };
    
    const std::string session_uuid = boost::uuids::to_string(get_session_uuid());

    // This coroutine should be really strict about checking 
    // the preconditions and postconditions for a file tranfer
    const FileStateView * curr_file_state = _parent_client_view->get_file_state(file_chunk_msg->filename());
    if (curr_file_state == nullptr
     || file_chunk_msg->session_id() != session_uuid
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
            session_uuid
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
        if (next_file_chunk.session_id() != session_uuid          
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
                session_uuid
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
            session_uuid
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
            session_uuid
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
        session_uuid
        );
    file_up_complete_msg.mutable_file_up_complete()->set_filename
        (
        file_chunk_msg->filename() 
        );
    file_up_complete_msg.mutable_file_up_complete()->set_success(true);
    co_await proto_io::send_msg(_session_socket, file_up_complete_msg);
    
    // I hate the fact I need the parent in order to do this call
    _parent_client_view->try_compile_for_active_sessions();

}   /* ServerSession::handle_file_transfer() */


