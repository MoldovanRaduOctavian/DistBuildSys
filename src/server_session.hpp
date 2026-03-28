#ifndef SERVER_SESSION_HPP
#define SERVER_SESSION_HPP

#include <mutex>
#include <string>

#include <boost/asio.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "distbuild_messages.pb.h"
#include "file_state_view.hpp"

class ClientView;

class ServerSession {

public:

    ServerSession
        (
        boost::asio::ip::tcp::socket && 
                                session_socket,
        ClientView *            parent_client_view,
        const boost::uuids::uuid &   
                                session_uuid 
        ) :
        _session_socket(std::move(session_socket)),
        _parent_client_view(parent_client_view),
        _session_uuid(session_uuid)
    {};
   
    void add_required_file_state
        (
        FileStateView & file_state
        )
    {
        std::lock_guard<std::mutex> lock(_mtx);
        _required_files_state.push_back(&file_state);
    }

    const std::vector<FileStateView *> &
        get_required_files_state() const
    {
        std::lock_guard<std::mutex> lock(_mtx);
        return _required_files_state;
    }
    
    boost::asio::ip::tcp::socket &
        get_session_socket()
    {
        std::lock_guard<std::mutex> lock(_mtx);
        return _session_socket;
    }
    
    boost::uuids::uuid get_session_uuid() const
    {
        std::lock_guard<std::mutex> lock(_mtx);
        return _session_uuid;    
    }
    
    const std::string & get_curr_working_dir() const
    {
        std::lock_guard<std::mutex> lock(_mtx);
        return _current_working_dir;
    }
     
    void start_session()
    {
        boost::asio::co_spawn(_session_socket.get_executor(), handle_session(), boost::asio::detached);
    }

    boost::asio::awaitable<void> handle_session();
    boost::asio::awaitable<void> handle_file_transfer
        (
        std::shared_ptr<distbuild::ClientFileChunkUploadRequest>
                    file_chunk_msg
        );

private:

    boost::asio::ip::tcp::socket    _session_socket;    
    ClientView *                    _parent_client_view;
    boost::uuids::uuid              _session_uuid;
    // A server UUID could be added too, but add it later
    
    // Files to be uploaded internal state
    std::vector<FileStateView *>    _required_files_state;    

    std::string                     _in_src_file;
    std::string                     _out_obj_file;
    std::string                     _compiler_name;
    std::string                     _current_working_dir;
    std::vector<std::string>        _cmd_line_args;
    int32_t                         _compiler_exit_code;
    std::string                     _compiler_stdout;
    std::string                     _compiler_stderr;

    uint64_t                        _compile_start_time;
    uint64_t                        _compile_duration;
    
    mutable std::mutex              _mtx;

    // An enum value with the current session state
    // in the client-server exchange
    // ... _session_state

};

#endif /* SERVER_SESSION_HPP */

