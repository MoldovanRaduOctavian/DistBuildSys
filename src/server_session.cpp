#include "server_session.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/dispatch.hpp>
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
#include "server.hpp"

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
    // std::cout << "CLIENT WORKING DIR: " << client_working_dir << '\n';
    std::string idirs_dir = _current_working_dir;
    _current_working_dir = cnvt_client_to_server_path(_current_working_dir, client_working_dir);    
    // std::cout << "SERVER WORKING DIR: " << _current_working_dir << '\n';
    
    std::lock_guard<std::mutex> lock(_mtx);

    // We need to get the object file full path
    _out_obj_file = generate_obj_file_path
                    (
                    ObjectStorage::OBJ_STORE_PATH,
                    boost::uuids::to_string(_parent_client_view->get_client_id()), 
                    boost::uuids::to_string(_session_uuid)
                    );
    
    // Create the obj store in here until we enable the cache
    std::filesystem::create_directories
        (
        std::filesystem::path(_out_obj_file).parent_path()
        );

    _cmd_line_args.clear();
    
    // Idirs -I<dir> and -isystem <dir> are the most important I think
    for (size_t idir_idx = 0; idir_idx < compiler_idirs.size();) {
        if (compiler_idirs[idir_idx].starts_with("-I")) {
            std::string i_path = 
                "-I" + cnvt_client_to_server_path(idirs_dir, compiler_idirs[idir_idx].substr(2));
            _cmd_line_args.emplace_back(i_path);
            ++idir_idx;
        }
        else {
            _cmd_line_args.emplace_back(compiler_idirs[idir_idx]);
            _cmd_line_args.emplace_back(
                cnvt_client_to_server_path(idirs_dir, compiler_idirs[idir_idx + 1])    
            );
            idir_idx += 2;
        }
    }

#if 0
    for (const std::string & cmd_line_arg : compiler_cmd_line_args) {

        _cmd_line_args.emplace_back
            ( 
            handle_ffile_prefix_map(cmd_line_arg, _current_working_dir)
            );
    }
#endif
    
    auto is_src_file = [](const std::string & file_name) {
        if (file_name.ends_with(".c")
         || file_name.ends_with(".cc")
         || file_name.ends_with(".cpp")
         || file_name.ends_with(".cxx")
        )
        {
            return true;
        }

        return false;

    };

    for (size_t arg_idx = 0; arg_idx < compiler_cmd_line_args.size(); ) {
        // std::cout << compiler_cmd_line_args[arg_idx] << '\n';
        if (is_src_file(compiler_cmd_line_args[arg_idx])) {
            ++arg_idx;
        }
        else if (compiler_cmd_line_args[arg_idx].ends_with("-o")) {
            arg_idx += 2;
        }
        else if (compiler_cmd_line_args[arg_idx].starts_with("/")){
            auto server_path =  
                cnvt_client_to_server_path(idirs_dir, compiler_cmd_line_args[arg_idx]);
            std::filesystem::create_directories
                (
                std::filesystem::path(server_path).parent_path()
                );

            _cmd_line_args.emplace_back
                (         
                server_path
                );
            ++arg_idx;
        }
        else {
            _cmd_line_args.emplace_back
                (         
                compiler_cmd_line_args[arg_idx]
                );
            ++arg_idx;
        }
    }

    _cmd_line_args.insert(_cmd_line_args.end(), {fixed_in_src_file, "-o", _out_obj_file});

#if 0
    std::cout << "\nREAL ARGS: \n";
    for (const auto & cmd_line_arg : _cmd_line_args) {
        std::cout << cmd_line_arg << "\n";
    }
#endif

}   /* ServerSession::preprocess_compiler_call() */


void ServerSession::try_request_src_compilation(bool lock_mtx) {
    
    // Be really careful about staying thread safe while accessing file data 
    // THIS CAUSES A DEADLOCK...DANGEROUS SITUATION
    if (lock_mtx) { 
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
                false, this, _compiler_name, _cmd_line_args, _current_working_dir
            });

            _is_compiler_running = true;
        }

    }    
    else {
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
                false, this, _compiler_name, _cmd_line_args, _current_working_dir
            });

            _is_compiler_running = true;
        }

    }


}   /* ServerSession::request_src_compilation() */

// This seems absolutely fucking horrendous to be called
// from the compiler manager
// we should use channels
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


void ServerSession::send_compilation_result()
{
    const bool compilation_failed = (_compiler_exit_code != 0);

    auto build_msg = [&]() -> distbuild::ServerMessage {
        distbuild::ServerMessage msg;

        auto * chunk = msg.mutable_obj_file_chunk_transmit();
        chunk->set_session_id(boost::uuids::to_string(_session_uuid));
        chunk->set_compiler_exit_code(_compiler_exit_code);
        chunk->set_compiler_call_duration(_compile_duration);

        return msg;
    };

    if (compilation_failed)
    {
        distbuild::ServerMessage msg = build_msg();
        auto* chunk = msg.mutable_obj_file_chunk_transmit();

        chunk->set_filesize(0);
        chunk->set_is_last_chunk(true);

        {
            std::lock_guard<std::mutex> lock(_mtx);
            chunk->set_compiler_stdout(_compiler_stdout);
            chunk->set_compiler_stderr(_compiler_stderr);
        }

        send_msg(msg);
        return;
    }

    std::ifstream obj_file_stream(_out_obj_file, std::ios::binary);
    if (!obj_file_stream)
    {
        // optional: handle error case
        return;
    }

    const uint64_t file_size =
        std::filesystem::file_size(_out_obj_file);

    constexpr size_t CHUNK_SIZE = 64 * 1024;
    std::vector<char> buffer(CHUNK_SIZE);

    while (true)
    {
        obj_file_stream.read(buffer.data(), CHUNK_SIZE);
        std::streamsize bytes_read = obj_file_stream.gcount();

        if (bytes_read <= 0) {
            break;
        }

        distbuild::ServerMessage msg = build_msg();
        auto * chunk = msg.mutable_obj_file_chunk_transmit();

        chunk->set_filesize(file_size);
        chunk->set_data(buffer.data(), bytes_read);
        chunk->set_is_last_chunk(bytes_read < (std::streamsize)CHUNK_SIZE);

        // attach logs only on last chunk
        if (chunk->is_last_chunk())
        {
            std::lock_guard<std::mutex> lock(_mtx);
            chunk->set_compiler_stdout(_compiler_stdout);
            chunk->set_compiler_stderr(_compiler_stderr);
        }

        send_msg(msg);
    }

}

boost::asio::awaitable<void> ServerSession::handle_server_session
    (
    std::shared_ptr<ServerSession>  self
    )
{
    try {
        // send_msg(*session_confirmed_msg);         
        std::cout << "A NEW SESSION HAS STARTED\n";
        while (!self->_terminated) {
            //receive and handle all types of packets from the client
            distbuild::ClientMessage client_message;
            co_await proto_io::receive_msg(self->_session_socket, client_message);
            switch (client_message.content_case()) {
                case distbuild::ClientMessage::kSessionStart:
                    self->_parent_client_view->update_last_active_ts();
                    break;

                case distbuild::ClientMessage::kFileChunkUpload: {
                    self->_parent_client_view->update_last_active_ts();

                    auto file_chunk_msg = 
                        std::make_shared<distbuild::ClientFileChunkUploadRequest>
                            (
                            client_message.file_chunk_upload()    
                            );
                    
                    // Does file_chunk_msg get invalidated by the time it
                    // is passed to process_file_chunk
                    bool chunk_upload_success = process_file_chunk(file_chunk_msg); 
                    if (chunk_upload_success == false) {
                        distbuild::ServerMessage session_abort_msg;
                        session_abort_msg.mutable_session_abort()->set_session_id
                            (
                            boost::uuids::to_string(self->_session_uuid)
                            );
                        
                        self->send_msg(session_abort_msg);

                    }

                    break;

                }
                
                case distbuild::ClientMessage::kSessionAbort:
                    // We need to dispose of this session
                    std::cout << "DO WE ALWAYS RECEIVE kSessionAbort ???\n";
                    self->terminate_server_session();
                    co_return;
                    break;

                default:
                    std::cout << "DO WE EVER GET A DEFAULT MESSAGE FROM CLIENT?\n";
                    self->terminate_server_session();
                    co_return;
                    break;
            }
        }

    }
    catch (std::exception & e) {
        // std::cout << e.what() << '\n';
        // terminate_server_session();
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

    // file_chunk_msg get invalidated at this point ?!?!?!
    const FileStateView * curr_file_state = _parent_client_view->get_file_state(file_chunk_msg->filename());
    if (curr_file_state == nullptr) {
        return false;
    }

    if (file_chunk_msg->session_id() != session_uuid) 
    {
        // This is really weird, curr_file_state == nullptr
        // should not be handled like this
        set_file_state
            (
            file_chunk_msg->filename(), 
            *curr_file_state,
            FileStateView::Status::FILE_STATUS_FAULT
            );
        
        std::cout << "VIATA MEA OF IMPART ALATURI DE VOLUNTARI [1]!\n";
        return false;
    }
    
    // It almost seems as if curr_file_state is not nullptr
    // but it still is an invalid pointer and that causes
    // the segmentation fault
    std::string server_file_name = curr_file_state->server_file_path;

        
    // BE CAREFUL WITH LOCKING TOO MUCH
    std::lock_guard<std::mutex> lock(_mtx);
    
    auto [transfer_state_it, ok] = _file_transfer_states.try_emplace
            (
            server_file_name,
            session_uuid,
            server_file_name
            );

    FileTransferState & transfer_state = transfer_state_it->second;

    if (transfer_state.is_finished == true) {
        // The file was either uploaded or failed
        // exit early

        // We need to do some extra things in here
        return true;
    }
     
    // We've got problem with path conversions client <-> server great
    auto client_filename = cnvt_server_to_client_path(_current_working_dir, transfer_state.filename);
    if (transfer_state.session_id != session_uuid
     || file_chunk_msg->sequence_no() != transfer_state.last_seq_no + 1
     || file_chunk_msg->offset() < transfer_state.last_offset
//     || file_chunk_msg->filename() != client_filename
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
            
            std::cout << "ServerSession::process_file_chunk SHA1 MISMATCH!!!\n";
            std::cout << "FILE SIZE: " << curr_file_state->file_sz_bytes << '\n';
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
        distbuild::ServerMessage server_msg{};
        auto * file_up_complete_msg = server_msg.mutable_file_up_complete();
        file_up_complete_msg->set_session_id(session_uuid);
        file_up_complete_msg->set_filename(file_chunk_msg->filename());
        file_up_complete_msg->set_success(true);
        send_msg(server_msg);

    }
    
    return true;   
    
}   /* ServerSession::process_file_chunk() */


void ServerSession::terminate_server_session() {
    
    std::lock_guard<std::mutex> lock(_mtx);    
    _terminated = true;
    
    // Accessing the object fields after this is particularly dangerous
    // Since it should be destroyed on removal
    
    _compiler_manager->invalidate_requests_for_session(this);
    _parent_client_view->remove_session(_session_uuid);


}   /* ServerSession::terminate_server_session() */


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
    
    auto self = shared_from_this();
    boost::asio::dispatch(_strand,
        [self, framed = std::move(framed)]() mutable {
            self->_write_queue.push_back(std::move(framed));
            if (!self->_write_in_progress) {
                self->_write_in_progress = true;
                boost::asio::co_spawn(
                    self->_strand,
                    self->_writer_loop(self),
                    boost::asio::detached
                );
            }
        }
    );

}   /* ServerSession::send_msg() */


boost::asio::awaitable<void> ServerSession::_writer_loop
    (
    std::shared_ptr<ServerSession>  self
    )
{
    try
    {
        while (!self->_write_queue.empty())
        { 
            std::string msg = std::move(self->_write_queue.front());
            self->_write_queue.pop_front();

            // release strand while doing I/O
            co_await boost::asio::async_write(
                self->_session_socket,
                boost::asio::buffer(msg),
                boost::asio::use_awaitable);
        }
    }
    catch (const std::exception& e)
    {
        std::cout << "writer error: " << e.what() << '\n';
    }

    self->_write_in_progress = false;
    if (!self->_write_queue.empty()) {
        self->_write_in_progress = true;
        boost::asio::co_spawn(
            self->_strand,
            self->_writer_loop(self),
            boost::asio::detached
        );
    }    

}

void ServerSession::set_available_compiler_jobs
    (
    size_t available_jobs
    ) 
{
    _server->set_advertiser_available_jobs(available_jobs);

}   /* ServerSession::set_available_compiler_jobs */



