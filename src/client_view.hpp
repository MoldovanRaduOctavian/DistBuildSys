#ifndef CLIENT_VIEW_HPP
#define CLIENT_VIEW_HPP

#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include "compiler_manager.hpp"
#include "distbuild_messages.pb.h"
#include "file_state_view.hpp"

class ServerSession;

// The view of a client state, from a server's perspective
class ClientView {

public:

    ClientView
        (
        const std::string & client_uuid,
        const std::string & current_working_dir
        ) :
        _client_uuid(boost::uuids::string_generator()(client_uuid)),        
        _current_working_dir(current_working_dir),
        _last_active_ts(std::chrono::steady_clock::now())
        {};

    // I will need an interface to insert file state views
    FileStateView & add_file_state
        (
        const std::string & client_path,
        uint64_t            file_sz_bytes,
        const std::string & file_sha1
        );
    
    const FileStateView * get_file_state
        (
        const std::string & client_path
        ) const
    {
        std::lock_guard<std::mutex> lock(_mtx);
        auto file_state_it = _client_file_states.find(client_path);
        
        if (file_state_it == _client_file_states.end()) {
            return nullptr;
        }

        return &file_state_it->second;
    }
    
    const boost::uuids::uuid & get_client_id() const
    {
        std::lock_guard<std::mutex> lock(_mtx);
        return _client_uuid;
    }

    bool update_file_state
        (
        const std::string &     client_path,
        const FileStateView &   updated_file_state
        )
    {
        std::lock_guard<std::mutex> lock(_mtx);
        auto file_state_it = _client_file_states.find(client_path);
        if (file_state_it == _client_file_states.end()) {
            return false;
        }

        file_state_it->second = updated_file_state;
        return true;

    }

    void add_session
        (
        boost::asio::ip::tcp::socket &&
                            session_socket,
        const distbuild::ClientSessionStartRequest &
                            session_start_msg,
        CompilerManager *   compiler_manager
        );
    
    void try_compile_for_active_sessions();

    const std::string & get_curr_working_dir() const {
        // This already produced a deadlock
        // We should use shared_lock when reading, most likely
        // std::lock_guard<std::mutex> lock(_mtx);
        return _current_working_dir;
    }
    
    void update_last_active_ts() {
        std::lock_guard<std::mutex> lock(_mtx);
        _last_active_ts = std::chrono::steady_clock::now();
    }

private:
        
    void _create_client_dir_hierarchy
        (
        const ServerSession & session
        );

    boost::uuids::uuid      _client_uuid;
    std::string             _current_working_dir;
    // When do I update this ???
    std::chrono::steady_clock::time_point
                            _last_active_ts;

    /* mapping from client file name to server file state */
    std::unordered_map<std::string, FileStateView>
                            _client_file_states;
    
    /* mapping from session uuid to ServerSession object */
    // a client might have multiple sessions active
    // at the same time
    // The client should own its sessions, not the server
    // The server should own the client storage though
    std::unordered_map<std::string, ServerSession>
                            _associated_sessions;
    
    // Does this client directory exist on the server side?
    // this is used to know which directories need to be 
    // created on the server side
    std::unordered_set<std::string>
                            _client_created_dirs;
    
    mutable std::mutex      _mtx;

};

#endif /* CLIENT_VIEW_HPP */

