#include "client_session.hpp"

#include <boost/asio/socket_base.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/detail/error_code.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>

#include <boost/process.hpp>
#include <boost/process/detail/child_decl.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <mutex>

#include "client.hpp"
#include "proto_io.hpp"
#include "unix_ipc_socket.hpp"

boost::asio::awaitable<void> ClientSession::_handle_client_session
    (
    std::shared_ptr<ClientSession> self
    )
{
   
    try {
        // This should end when the compilation is done
        self->_send_required_file();        
        while (!self->_terminated) {
            
            distbuild::ServerMessage server_message;
            co_await proto_io::receive_msg(self->_session_socket, server_message);
            switch (server_message.content_case()) {
                case distbuild::ServerMessage::kSessionConfirmed:
                    break;
                case distbuild::ServerMessage::kSessionAbort:
                    // What do you do in case of abort???
                    co_await self->perform_local_compilation();
                    self->terminate_client_session();
                    co_return;
                    break;
                case distbuild::ServerMessage::kFileUpComplete:
                    // Do we even check the contents of this packet?
                    if (server_message.file_up_complete().session_id() 
                        ==  boost::uuids::to_string(self->_session_uuid)
                     && server_message.file_up_complete().success() == true) {
                        _send_required_file();
                    }
                    else {
                        // What do we do?
                        // Do we retry sending that packet?
                        // Do we go for local compilation?
                        // Do we end the session?
                        co_await self->perform_local_compilation();
                        self->send_abort_to_server();
                        self->terminate_client_session();
                        co_return;
                    }

                    break;
                case distbuild::ServerMessage::kAllReqUpComplete:
                    // We do not even need this I think
                    break;
                case distbuild::ServerMessage::kObjFileChunkTransmit: {
                    // This is the most important one
                    auto obj_chunk_msg = 
                        std::make_shared<distbuild::ServerObjFileChunkResponse>(
                            server_message.obj_file_chunk_transmit()
                        );

                    bool obj_chunk_upload_success = 
                        co_await self->_process_obj_file_chunk(obj_chunk_msg);

                    if (obj_chunk_upload_success == false) {
                        // Handle failure gracefully
                        // Fall back to local compilation
                        // Cleanup the current session
                        co_await self->perform_local_compilation();
                        self->send_abort_to_server();
                        self->terminate_client_session();
                        co_return;
                    }

                    break;
                }
                default:
                    break;
            }

        } 
    }
    catch (const std::exception & e) {
        // What do we do in case of an exception?
        std::cout << "THIS ONLY OCCURS IN handle_client_session\n";
        std::cout << e.what() << '\n';
        // co_await perform_local_compilation();
        // co_await terminate_client_session();
    }
    
    // co_await terminate_client_session();

}   /* ClientSession::_handle_client_session() */

void ClientSession::send_abort_to_server() {
    try {
        distbuild::ClientMessage client_message;
        auto * client_session_abort_rqst = client_message.mutable_session_abort();
        client_session_abort_rqst->set_session_id(boost::uuids::to_string(_session_uuid));
        client_session_abort_rqst->set_client_id
            (
            boost::uuids::to_string(_client.get_client_uuid())
            );
        send_msg(client_message);

    }
    catch (const std::exception & e) {
        std::cout << "Inside send_abort_to_server: " << e.what() << '\n';
    }

}   /* ClientSession::send_abort_to_server() */

void ClientSession::terminate_client_session() {
    
    std::lock_guard<std::mutex> lock(_mtx);
    _terminated = true;    
    // Wait until writing ends     
    _client.remove_client_session(_session_uuid);
    std::cout << "DO WE ALWAYS GET AT THE END OF terminate_client_session?\n";

}   /* ClientSession::terminate_client_session() */


boost::asio::awaitable<void> ClientSession::perform_local_compilation() {
    boost::process::ipstream stdout_stream;
    boost::process::ipstream stderr_stream;
        
    boost::process::child compiler_process
        (
        _compiler_call.get_compiler_type(),
        boost::process::args(_compiler_call.get_cmd_line_args()),
        boost::process::std_out > stdout_stream,
        boost::process::std_err > stderr_stream,
        boost::process::start_dir = _compiler_call.get_current_working_dir() 
        );
     
    compiler_process.wait();

    std::ostringstream stdout_oss;
    stdout_oss << stdout_stream.rdbuf();
    
    std::ostringstream stderr_oss;
    stderr_oss << stderr_stream.rdbuf(); 

    auto compilation_end_ts = std::chrono::steady_clock::now();
    auto compilation_duration = std::chrono::duration_cast<std::chrono::seconds>
        (
        compilation_end_ts - _compiler_call.get_call_creation_time()
        );

    int compiler_exit_code = compiler_process.exit_code();
    
    const std::string stdout_str = stdout_oss.str();
    const std::string stderr_str = stderr_oss.str();

    _compiler_call.set_call_duration(compilation_duration);
    _compiler_call.set_exit_code(compiler_exit_code);
    _compiler_call.set_stdout_content(stdout_str);
    _compiler_call.set_stderr_content(stderr_str);
    
    co_await publish_unix_ipc_response(UnixIpcResponse{
        compiler_exit_code,
        stdout_str,
        stderr_str
    });

}   /* ClientSession::perform_local_compilation() */


boost::asio::awaitable<bool> ClientSession::_process_obj_file_chunk
    (
    std::shared_ptr<distbuild::ServerObjFileChunkResponse>
                obj_file_chunk_msg
    )
{
    const std::string session_uuid = boost::uuids::to_string(get_session_uuid());
    
    if (obj_file_chunk_msg->session_id() != session_uuid) {
        // ... Handle error cases gracefully, especially on the client
        // any failure should trigger a local compilation
        co_return false;
    }
    
    std::lock_guard<std::mutex> lock(_mtx);
    if (_obj_file_transfer_state.has_value()) {
        if (_obj_file_transfer_state.value().is_finished == true) {
            co_return true;
        }
    }
    else {
        // Most likely just fall back to local compilation
        co_return false;
    }
    
    ObjFileTransferState & obj_transfer_state = _obj_file_transfer_state.value();
     
    if (obj_transfer_state.session_id != session_uuid) {
        obj_transfer_state.is_finished = true;
        obj_transfer_state.obj_out_stream.close();
        co_return false;
    }    
    
    if (!obj_transfer_state.obj_out_stream) {
        std::cout << "INVALID OBJECT FILENAME: " << obj_transfer_state.filename << '\n';
        throw std::runtime_error{"INVALID OBJECT FILE EXCEPTION!"};
    }

    obj_transfer_state.obj_out_stream.write
        (
        obj_file_chunk_msg->data().data(),
        obj_file_chunk_msg->data().size()
        );

    if (obj_file_chunk_msg->is_last_chunk()) {
        obj_transfer_state.is_finished = true;        
        obj_transfer_state.obj_out_stream.close();
        
        // This is where you liquidate the client session
        // and the server session, and send the results
        // to the compiler wrapper by UNIX IPC socket
        // We have to signal the client to publish the results
        // through the UNIX IPC socket/sockets
        
        _compiler_call.set_call_duration(std::chrono::seconds(
            obj_file_chunk_msg->compiler_call_duration()
        ));
        _compiler_call.set_exit_code(obj_file_chunk_msg->compiler_exit_code());
        _compiler_call.set_stderr_content(obj_file_chunk_msg->compiler_stderr());
        _compiler_call.set_stdout_content(obj_file_chunk_msg->compiler_stdout());

        co_await publish_unix_ipc_response(UnixIpcResponse{
            obj_file_chunk_msg->compiler_exit_code(),
            obj_file_chunk_msg->compiler_stderr(),
            obj_file_chunk_msg->compiler_stdout()
        });

    }
    
    co_return true;

}   /* ClientSession::_process_obj_file_chunk() */



boost::asio::awaitable<bool> ClientSession::start_client_session
    (
    const std::string &     server_ip,
    uint16_t                server_port,
    const distbuild::ClientMessage & 
                            session_start_rqst,
    boost::uuids::uuid &    session_uuid
    )
{
    // Most likely this should be wrapped in a try/catch
    // as asio function throw on error
    // we will see if we can live without that
    co_await _session_socket.async_connect
        (
        boost::asio::ip::tcp::endpoint
            (
            boost::asio::ip::make_address(server_ip),
            server_port
            ),
        boost::asio::use_awaitable
        );
    
    std::cout << "ClientSession::start_client_session right after async_connect\n";
    distbuild::ServerMessage session_confirmation_msg;
    send_msg(session_start_rqst); 
    // We fail to receive the session confirmed message ???
    co_await proto_io::receive_msg(_session_socket, session_confirmation_msg);
    if (session_confirmation_msg.content_case() == distbuild::ServerMessage::kSessionConfirmed) {
        _session_uuid = boost::uuids::string_generator() 
            (session_confirmation_msg.session_confirmed().session_id());
        session_uuid = _session_uuid;
        _obj_file_transfer_state = ObjFileTransferState
                    (
                    boost::uuids::to_string(_session_uuid), 
                    std::filesystem::weakly_canonical(
                        std::filesystem::path{_compiler_call.get_current_working_dir()} /
                        std::filesystem::path{_compiler_call.get_output_obj_file()}
                    )
                    );
        
        std::cout << "CURRENT WORKING DIR: " << _compiler_call.get_current_working_dir() << '\n';
        std::cout << "OBJ FILE TO BE WRITTEN:" << _obj_file_transfer_state->filename << '\n';

        const distbuild::ServerSessionConfirmedResponse & session_confirmed =
            session_confirmation_msg.session_confirmed();
        
        _required_files = std::vector<std::string>
            {
            session_confirmed.required_files().begin(),
            session_confirmed.required_files().end()
            };

        auto self = shared_from_this();
        boost::asio::co_spawn
            (
            _session_socket.get_executor(), 
            _handle_client_session(self), 
            boost::asio::detached
            );
        
        co_return true;

    }
    else {
        // What do we do in case of failure?
        std::cout << "ClientSession::start_client_session FAILURE!!!!\n";
        co_return false;
    } 
    
}   /* ClientSession::start_client_session() */


void ClientSession::_send_required_file() {
    
    // Second time when you want to compile the same
    // file, _required_files size is 0, you do not
    // send any message because you exit early
    // and the session FSM locks up
    if (_req_files_send_idx >= _required_files.size()) {
        return;
    }

    const std::string& required_file =
        _required_files[_req_files_send_idx];

    std::ifstream requested_file_stream(
        required_file,
        std::ios::binary);

    if (!requested_file_stream) {
        return;
    }

    constexpr size_t OBJ_CHUNK_SZ = 64 * 1024;
    std::vector<char> chunk_buff(OBJ_CHUNK_SZ);

    uint64_t seq_no = 1;
    uint64_t file_offset = 0;

    std::string str_session_uuid =
        boost::uuids::to_string(_session_uuid);

    //
    // Special case: empty file
    //

    requested_file_stream.seekg(0, std::ios::end);

    if (requested_file_stream.tellg() == 0)
    {
        std::cout << "THIS IS AN EMPTY C++ SRC WHAT DO WE DO?\n";
        distbuild::ClientMessage msg;
        auto* chunk = msg.mutable_file_chunk_upload();

        chunk->set_session_id(str_session_uuid);
        chunk->set_filename(required_file);
        chunk->set_sequence_no(1);
        chunk->set_offset(0);
        chunk->set_is_last_chunk(true);

        send_msg(msg);

        ++_req_files_send_idx;
        return;
    }

    requested_file_stream.seekg(0, std::ios::beg);

    while (requested_file_stream.read(chunk_buff.data(), OBJ_CHUNK_SZ)
           || requested_file_stream.gcount())
    {
        auto bytes_read =
            static_cast<size_t>(requested_file_stream.gcount());

        distbuild::ClientMessage msg;
        auto* chunk = msg.mutable_file_chunk_upload();

        chunk->set_session_id(str_session_uuid);
        chunk->set_filename(required_file);
        chunk->set_sequence_no(seq_no++);
        chunk->set_offset(file_offset);
        chunk->set_data(chunk_buff.data(), bytes_read);
        chunk->set_is_last_chunk(bytes_read < OBJ_CHUNK_SZ);

        file_offset += bytes_read;

        send_msg(msg);
    }

    ++_req_files_send_idx;

}   /* ClientSession::_send_required_file() */


void ClientSession::send_msg
    (
    const distbuild::ClientMessage & msg
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
    });
 
}   /* ClientSession::send_msg() */


boost::asio::awaitable<void> ClientSession::_writer_loop
    (
    std::shared_ptr<ClientSession>  self
    ) 
{

    try {
        while (!self->_write_queue.empty()) {
            std::string & msg = self->_write_queue.front();
            
            co_await boost::asio::async_write(
                self->_session_socket,
                boost::asio::buffer(msg),
                boost::asio::use_awaitable
            );

            self->_write_queue.pop_front();
        }
    }
    catch (std::exception & e) {
        std::cout << e.what() << '\n';
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
 
}   /* ClientSession::_writer_loop() */

