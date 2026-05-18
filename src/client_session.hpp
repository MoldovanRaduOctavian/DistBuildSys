#ifndef CLIENT_SESSION_HPP
#define CLIENT_SESSION_HPP

#include <atomic>
#include <fstream>
#include <deque>
#include <mutex>
#include <memory>
#include <string>
#include <optional>

#include <boost/asio/experimental/channel.hpp>
#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>

#include "compiler_call.hpp"
#include "distbuild_messages.pb.h"
#include "unix_ipc_socket.hpp"

class Client;

class ClientSession {
    
    struct ObjFileTransferState {
        bool            is_finished;
        std::ofstream   obj_out_stream;
        std::string     session_id;
        std::string     filename;
        uint64_t        last_seq_no;
        uint64_t        last_offset;

        ObjFileTransferState
            (
            const std::string & _session_id,
            const std::string & _filename
            ) :
            is_finished(false),
            obj_out_stream(_filename, std::ios::binary),
            session_id(_session_id),
            filename(_filename),
            last_seq_no(0),
            last_offset(0)
        {};

    };

public:

    ClientSession
        (
        boost::asio::io_context & 
                            io_ctx,
        Client &            client,
        CompilerCall &      compiler_call
        ) :
        _io_ctx(io_ctx),
        _session_socket(_io_ctx),
        _client(client),
        _compiler_call(compiler_call),
        _req_files_send_idx(0),
        _unix_ipc_channel(_io_ctx, 1)
    {};
    
    void initialize_client_session
        (
        const boost::uuids::uuid & session_uuid
        );

    void send_msg
        (
        const distbuild::ClientMessage & msg
        );
         
    boost::asio::awaitable<bool> start_client_session
        (
        const std::string &     server_ip,
        uint16_t                server_port,
        const distbuild::ClientMessage & 
                                session_start_rqst,
        boost::uuids::uuid &    session_uuid
        );
    
    const boost::uuids::uuid & get_session_uuid() const {
        std::lock_guard<std::mutex> lock(_mtx);
        return _session_uuid;
    }
    
    
    boost::asio::awaitable<void> perform_local_compilation();
    
    void terminate_client_session();
    
    boost::asio::awaitable<UnixIpcResponse> retrieve_unix_ipc_response() {
        UnixIpcResponse response = 
            co_await _unix_ipc_channel.async_receive(boost::asio::use_awaitable);

        co_return response;

    }

    boost::asio::awaitable<void> publish_unix_ipc_response(UnixIpcResponse && response) {
        co_await _unix_ipc_channel.async_send
            (
            boost::system::error_code{},
            response, 
            boost::asio::use_awaitable
            );   

    }

private:
    

    boost::asio::awaitable<bool> _process_obj_file_chunk
        (
        std::shared_ptr<distbuild::ServerObjFileChunkResponse>
                    obj_file_chunk_msg
        );
    
    void                            _send_required_file();
    
    boost::asio::awaitable<void>    _handle_client_session();

    boost::asio::awaitable<void>    _writer_loop();
    
    // This most likely will hold a handle to an invocation object

    boost::asio::io_context &       _io_ctx;
    boost::asio::ip::tcp::socket    _session_socket;
    
    CompilerCall &                  _compiler_call;
    
    Client &                        _client;

    boost::asio::strand<boost::asio::any_io_executor>
                                    _strand{ _session_socket.get_executor() };
    std::deque<std::string>         _write_queue;
    std::atomic<bool>               _write_in_progress;    
    
    boost::uuids::uuid              _session_uuid;

    std::vector<std::string>        _required_files;
    std::atomic<size_t>             _req_files_send_idx;
    
    std::optional<ObjFileTransferState>
                                    _obj_file_transfer_state;
    
    boost::asio::experimental::channel<void(boost::system::error_code, UnixIpcResponse)>
                                    _unix_ipc_channel;

    mutable std::mutex              _mtx;

};

#endif /* CLIENT_SESSION_HPP */

