#include "server_session.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include <boost/uuid/uuid_io.hpp>

#include "client_view.hpp"
#include "compiler_manager.hpp"
#include "distbuild_messages.pb.h"
#include "file_state_view.hpp"
#include "object_storage.hpp"
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
    auto handle_ffile_prefix_map = [](const std::string & cmd_line_arg,
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

    // This needs to get figured out
    // _current_working_dir for the session can be retrieved from ClientView
    // _current_working_dir = cnvt_client_to_server_path(_current_working_dir, client_working_dir);    
    
    std::lock_guard<std::mutex> lock(_mtx);

    // We need to get the object file full path
    _out_obj_file = generate_obj_file_path
                    (
                    ObjectStorage::OBJ_STORE_PATH,
                    boost::uuids::to_string(_parent_client_view->get_client_id()), 
                    boost::uuids::to_string(_session_uuid)
                    );

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
    send_compilation_result();

}   /* ServerSession::publish_compilation_results() */


void ServerSession::send_compilation_result() {
    
    // What if the session socket gets used simulteneously form multiple threads???
    // That seems like a dangerous situation
    if (_compiler_exit_code != 0) {
        // Handle the case in which compilation has failed
        distbuild::ServerMessage fail_obj_file_response{};
        {
            std::lock_guard<std::mutex> lock(_mtx);
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
        }

        // Sending from multiple threads is inherently dangerous
        // co_await proto_io::send_msg(_session_socket, fail_obj_file_response);
        send_msg(fail_obj_file_response);
    }
    
    else {
        // Send out the object file in chunks...
        std::ifstream obj_file_stream(_out_obj_file, std::ios::binary);
        uint64_t obj_file_sz = std::filesystem::file_size(_out_obj_file);
        

        const size_t OBJ_CHUNK_SZ = 64 * 1024;
        std::vector<char> chunk_buff(OBJ_CHUNK_SZ);
        while (obj_file_stream.read(chunk_buff.data(), OBJ_CHUNK_SZ)
               || obj_file_stream.gcount())
        {
            distbuild::ServerMessage obj_file_response{};
            {
                std::lock_guard<std::mutex> lock(_mtx);
                obj_file_response.mutable_obj_file_chunk_transmit()
                    ->set_session_id(boost::uuids::to_string(_session_uuid));
                obj_file_response.mutable_obj_file_chunk_transmit()
                    ->set_filesize(obj_file_sz);
                obj_file_response.mutable_obj_file_chunk_transmit()
                    ->set_compiler_exit_code(_compiler_exit_code);
                obj_file_response.mutable_obj_file_chunk_transmit()
                    ->set_compiler_call_duration(_compile_duration);

                if (obj_file_stream.gcount() < OBJ_CHUNK_SZ) {
                    obj_file_response.mutable_obj_file_chunk_transmit()
                        ->set_data(chunk_buff.data(), obj_file_stream.gcount());
                    obj_file_response.mutable_obj_file_chunk_transmit()
                        ->set_compiler_stdout(_compiler_stdout);
                    obj_file_response.mutable_obj_file_chunk_transmit()
                        ->set_compiler_stderr(_compiler_stderr);
                    obj_file_response.mutable_obj_file_chunk_transmit()
                        ->set_is_last_chunk(true);
                }
                else {
                    obj_file_response.mutable_obj_file_chunk_transmit()
                        ->set_data(chunk_buff.data(), obj_file_stream.gcount());
                    obj_file_response.mutable_obj_file_chunk_transmit()
                        ->set_compiler_stdout(_compiler_stdout);
                    obj_file_response.mutable_obj_file_chunk_transmit()
                        ->set_is_last_chunk(false);
                }

            }            
            
            // co_await proto_io::send_msg(_session_socket, obj_file_response);
            send_msg(obj_file_response);
            chunk_buff.clear();
        }
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
                // Every time we receive a packet from a client, update its 
                // last active timestamp
                _parent_client_view->update_last_active_ts();
                break;
            case distbuild::ClientMessage::kFileChunkUpload: {
                // Do we use co_await for this, or co_spawn ?
                // Calling read from multiple threads is really
                // dangerous apparently
                // this does not allow concurrent file uploading either
                _parent_client_view->update_last_active_ts();

                auto file_chunk_msg = 
                    std::make_shared<distbuild::ClientFileChunkUploadRequest>
                        (
                        client_message.file_chunk_upload()    
                        );
                
                bool chunk_upload_success = process_file_chunk(file_chunk_msg); 
                if (chunk_upload_success == false) {
                    distbuild::ServerMessage session_abort_msg;
                    session_abort_msg.mutable_session_abort()->set_session_id
                        (
                        boost::uuids::to_string(_session_uuid)
                        );
                    
                    // Sending messages on multiple threads is inherently dangerous
                    // co_await proto_io::send_msg(_session_socket, session_abort_msg);
                    send_msg(session_abort_msg);

                }

                break;

            }

            default:
                break;
        }
    }

}   /* ServerSession::handle_session() */


bool ServerSession::process_file_chunk
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
        set_file_state
            (
            file_chunk_msg->filename(), 
            *curr_file_state,
            FileStateView::Status::FILE_STATUS_FAULT
            );

        return false;
    }
 
    std::string server_file_name = curr_file_state->server_file_path;

        
    // BE CAREFUL WITH LOCKING TOO MUCH
    std::lock_guard<std::mutex> lock(_mtx);

    auto transfer_state_it = _file_transfer_states.find(server_file_name);
    if (transfer_state_it == _file_transfer_states.end()) {
        _file_transfer_states.try_emplace
            (
            server_file_name,
            session_uuid,
            server_file_name
            );

        transfer_state_it = _file_transfer_states.find(server_file_name);

    }
    
    FileTransferState & transfer_state = transfer_state_it->second;
    if (transfer_state.is_finished == true) {
        // The file was either uploaded or failed
        // exit early
        return true;
    }

    if (transfer_state.session_id != session_uuid
     || file_chunk_msg->sequence_no() != transfer_state.last_seq_no + 1
     || file_chunk_msg->offset() <= transfer_state.last_offset
     || file_chunk_msg->filename() != transfer_state.filename
    )
    {
        transfer_state.is_finished = true;
        transfer_state.file_out_stream.close();
        // The session will be stopped on chunk upload failure
        set_file_state
            (
            file_chunk_msg->filename(), 
            *curr_file_state,
            FileStateView::Status::FILE_STATUS_FAULT
            );

        return false;
    }
    
    transfer_state.last_offset = file_chunk_msg->offset();
    transfer_state.last_seq_no = file_chunk_msg->sequence_no();
    
    transfer_state.file_out_stream.write(file_chunk_msg->data().data(), file_chunk_msg->data().size());

    if (file_chunk_msg->is_last_chunk()) {
        // Close the ostream to the file...
        transfer_state.is_finished = true;
        transfer_state.file_out_stream.close();

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
            
            return false;

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
            
            return false;
        }    


        set_file_state
            (
            file_chunk_msg->filename(), 
            *curr_file_state,
            FileStateView::Status::FILE_STATUS_AVAILABLE
            );
        
        _parent_client_view->try_compile_for_active_sessions();

        // DO NOT FORGET TO SEND A FILE COMPLETE MESSAGE BACK TO THE CLIENT
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
        send_msg(file_up_complete_msg);

    }
    
    return true;   
    
}   /* ServerSession::process_file_chunk() */


void ServerSession::send_msg
    (
    const distbuild::ServerMessage & msg
    )
{
    std::string msg_payload;
    msg.SerializeToString(&msg_payload);

    uint32_t network_msg_sz = htonl(msg_payload.size());
    
    std::string framed;
    framed.append(reinterpret_cast<char *>(&network_msg_sz), sizeof(network_msg_sz));
    framed.append(msg_payload);

    boost::asio::dispatch(_strand,
        [this, framed = std::move(framed)]() mutable {
            _write_queue.push_back(std::move(framed));
            if (!_write_in_progress) {
                _write_in_progress = true;
                boost::asio::co_spawn(
                    _strand,
                    _writer_loop(),
                    boost::asio::detached
                );
            }
    });

}   /* ServerSession::send_msg() */


boost::asio::awaitable<void> ServerSession::_writer_loop() {
    
    try {
        while (!_write_queue.empty()) {
            std::string & msg = _write_queue.front();

            co_await boost::asio::async_write(
                _session_socket,
                boost::asio::buffer(msg),
                boost::asio::use_awaitable
            );

            _write_queue.pop_front();
        }
    }
    catch (std::exception & e) {
        // How do we handle such exceptions ?
    }

}   /* ServerSession::_writer_loop() */

