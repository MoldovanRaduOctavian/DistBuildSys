#include "client_session.hpp"

#include <fstream>

#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "proto_io.hpp"

boost::asio::awaitable<void> ClientSession::_handle_client_session() {
    
    for (;;) {
        
        distbuild::ServerMessage server_message;
        co_await proto_io::receive_msg(_session_socket, server_message);
        switch (server_message.content_case()) {
            case distbuild::ServerMessage::kSessionConfirmed:
                break;
            case distbuild::ServerMessage::kSessionAbort:
                break;
            case distbuild::ServerMessage::kFileUpComplete:
                // Do we even check the contents of this packet?
                if (server_message.file_up_complete().session_id() ==  boost::uuids::to_string(_session_uuid)
                 && server_message.file_up_complete().success() == true) {
                    _send_required_file();
                }
                else {
                    // What do we do?
                    // Do we retry sending that packet?
                    // Do we go for local compilation?
                    // Do we end the session?
                }

                break;
            case distbuild::ServerMessage::kAllReqUpComplete:
                // We do not even need this I think
                break;
            case distbuild::ServerMessage::kObjFileChunkTransmit:
                // This is the most important one
                break;

            default:
                break;
        }

    } 

}   /* ClientSession::_handle_client_session() */


bool ClientSession::_process_obj_file_chunk
    (
    std::shared_ptr<distbuild::ServerObjFileChunkResponse>
                obj_file_chunk_msg
    )
{
    const std::string session_uuid = boost::uuids::to_string(get_session_uuid());
    
    if (obj_file_chunk_msg->session_id() != session_uuid) {
        // ... Handle error cases gracefully, especially on the client
        // any failure should trigger a local compilation
        return false;
    }
    
    std::lock_guard<std::mutex> lock(_mtx);
    if (_obj_file_transfer_state.has_value()) {
        if (_obj_file_transfer_state.value().is_finished == true) {
            return true;
        }
    }
    else {
        // Most likely just fall back to local compilation
        return false;
    }
    
    ObjFileTransferState & obj_transfer_state = _obj_file_transfer_state.value();
     
    if (obj_transfer_state.session_id != session_uuid) {
        obj_transfer_state.is_finished = true;
        obj_transfer_state.obj_out_stream.close();
        return false;
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

    }
    
    return true;

}   /* ClientSession::_handle_client_session() */



boost::asio::awaitable<bool> ClientSession::start_client_session
    (
    const std::string &     server_ip,
    uint16_t                server_port,
    const distbuild::ClientMessage & 
                            session_start_rqst,
    boost::uuids::uuid &    session_uuid
    )
{
    co_await _session_socket.async_connect
        (
        boost::asio::ip::tcp::endpoint
            (
            boost::asio::ip::make_address(server_ip),
            server_port
            ),
        boost::asio::use_awaitable
        );


    send_msg(session_start_rqst);

    distbuild::ServerMessage session_confirmation_msg;
    co_await proto_io::receive_msg(_session_socket, session_confirmation_msg);
    if (session_confirmation_msg.content_case() == distbuild::ServerMessage::kSessionConfirmed) {
        _session_uuid = boost::uuids::string_generator() 
            (session_confirmation_msg.session_confirmed().session_id());
        session_uuid = _session_uuid;
        _obj_file_transfer_state = ObjFileTransferState
                    (
                    boost::uuids::to_string(_session_uuid), 
                    _compiler_call.get_input_src_file()
                    );

        boost::asio::co_spawn
            (
            _session_socket.get_executor(), 
            _handle_client_session(), 
            boost::asio::detached
            );
        
        const distbuild::ServerSessionConfirmedResponse & session_confirmed =
            session_confirmation_msg.session_confirmed();
        
        _required_files = std::vector<std::string>
            {
            session_confirmed.required_files().begin(),
            session_confirmed.required_files().end()
            };
        
        _send_required_file();
        co_return true;

    }
    else {
        // What do we do in case of failure?
        co_return false;
    } 
    
}   /* ClientSession::start_client_session() */


void ClientSession::_send_required_file() {
    
    // DO we need mutex protection for these functions?
    if (_req_files_send_idx < _required_files.size()) {
        const std::string & required_file = _required_files[_req_files_send_idx];
        
        std::ifstream requested_file_stream(required_file, std::ios::binary);
        const size_t OBJ_CHUNK_SZ = 64 * 1024;
        std::vector<char> chunk_buff(OBJ_CHUNK_SZ);        
        uint64_t seq_no = 1;
        uint64_t file_offset = 0;
        
        std::string str_session_uuid = boost::uuids::to_string(_session_uuid);
        while (requested_file_stream.read(chunk_buff.data(), OBJ_CHUNK_SZ)
                || requested_file_stream.gcount())
            {
                auto bytes_read = requested_file_stream.gcount();

                distbuild::ClientMessage requested_file_chunk_msg;
                auto * chunk = requested_file_chunk_msg.mutable_file_chunk_upload();

                chunk->set_session_id(str_session_uuid);
                chunk->set_filename(required_file);
                chunk->set_sequence_no(seq_no++);
                chunk->set_offset(file_offset);
                chunk->set_data(chunk_buff.data(), bytes_read);
                chunk->set_is_last_chunk(bytes_read < OBJ_CHUNK_SZ);

                file_offset += bytes_read;
                send_msg(requested_file_chunk_msg);

            }
 
        ++_req_files_send_idx;
    } 

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
 
}   /* ClientSession::send_msg() */


boost::asio::awaitable<void> ClientSession::_writer_loop() {

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
        std::cout << e.what() << '\n';
    }

    _write_in_progress = false;
 
}   /* ClientSession::_writer_loop() */

