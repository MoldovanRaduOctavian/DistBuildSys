#include "client_view.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <filesystem>
#include <unordered_set>

#include "distbuild_messages.pb.h"
#include "file_state_view.hpp"
#include "proto_io.hpp"
#include "server_session.hpp"


void ClientView::_create_client_dir_hierarchy
    (
    const ServerSession & session
    )
{
    std::unordered_set<std::string> session_dirs_to_create;
    {
        std::lock_guard<std::mutex> lock(_mtx);     
        if (!_client_created_dirs.contains(session.get_curr_working_dir())) {
            session_dirs_to_create.insert(session.get_curr_working_dir());
        }

        for (const FileStateView * file_state : session.get_required_files_state()) {
            std::string parent_dir = 
                std::filesystem::path(file_state->server_file_path).parent_path().string();
            
            if (!_client_created_dirs.contains(parent_dir)) {
                session_dirs_to_create.insert(parent_dir);
            }
            
        }
    }

    for (const std::string & dir_to_create : session_dirs_to_create) {
        std::filesystem::create_directories
            (
            std::filesystem::path(dir_to_create)
            );
    }

    {
        std::lock_guard<std::mutex> lock(_mtx);
        for (const std::string & created_dir : session_dirs_to_create) {
            _client_created_dirs.emplace(created_dir);
        }

    }

}   /* ClientView::_create_client_dir_hierarchy() */


FileStateView & ClientView::add_file_state
    (
    const std::string & client_path,
    uint64_t            file_sz_bytes,
    const std::string & file_sha1
    )
{
    std::lock_guard<std::mutex> lock(_mtx);
    auto file_state_pair = _client_file_states.find(client_path);
    if (file_state_pair != _client_file_states.end()
        && file_state_pair->second.file_sha1 == file_sha1
        && file_state_pair->second.file_sz_bytes == file_sz_bytes
        )
    {
        return file_state_pair->second;
    }
    

    // Add a new FileStateView 
    // Horrible variable naming
    auto [file_state_it, ok] = _client_file_states.try_emplace
        (
        client_path,
        file_sz_bytes,          
        file_sha1,
        cnvt_client_to_server_path(_current_working_dir, client_path)    
        );
    
    return file_state_it->second;

}   /* ClientView::add_file_state() */


boost::asio::awaitable<void> ClientView::add_session
    (
    boost::asio::ip::tcp::socket &&
                        session_socket,
    const distbuild::ClientSessionStartRequest &
                        session_start_msg
    )
{
    boost::uuids::uuid session_uuid = boost::uuids::random_generator()();
    
    {
        std::lock_guard<std::mutex> lock(_mtx);
        auto [session_it, ok] = _associated_sessions.try_emplace
            (
            boost::uuids::to_string(session_uuid),
            std::move(session_socket),
            this,
            session_uuid
            );
       
        for (const distbuild::FileInfo & file_info : session_start_msg.required_files_info()) {
            // The client view holds a reference to all the files, from sessions
            FileStateView & new_file_state = 
                add_file_state(file_info.filename(), file_info.filesize(), file_info.filehash()); 
            session_it->second.add_required_file_state(new_file_state);

        }
    
    }

    // Try to create the directory of the client associated to the
    
    ServerSession & new_session = _associated_sessions.find(boost::uuids::to_string(session_uuid))->second;
    _create_client_dir_hierarchy(new_session);    
    
    std::vector<std::string> requested_files_client_paths;
    for (const FileStateView * file_info : new_session.get_required_files_state()) {
        switch (file_info->file_status) {
            case FileStateView::Status::FILE_STATUS_INITIAL: {
                FileStateView updated_file_state = *file_info;
                updated_file_state.file_status = FileStateView::Status::FILE_STATUS_UPLOADING;
                updated_file_state.up_start_time = 
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()
                    ).count();
                
                std::string file_client_path = 
                    _cnvt_server_to_client_path(_current_working_dir, updated_file_state.server_file_path);

                update_file_state
                    (
                    file_client_path,
                    updated_file_state
                    );                 
                 
                // Also try to handle file caching in here?
                requested_files_client_paths.emplace_back(file_client_path);
                break;
            }

            case FileStateView::Status::FILE_STATUS_UPLOADING:
                break;
            case FileStateView::Status::FILE_STATUS_AVAILABLE:
                // Do I have to check if the file has changed?
                // That is not a bad idea
                break;
            case FileStateView::Status::FILE_STATUS_FAULT: { 
                // This seems tricky to handle
                requested_files_client_paths.emplace_back
                    (
                    _cnvt_server_to_client_path(_current_working_dir, file_info->server_file_path)
                    );
                break;
            }
            default:
                break;
        }         
    }     

    distbuild::ServerMessage session_confirmed_msg;
    session_confirmed_msg.mutable_session_confirmed()->set_session_id
        (
        boost::uuids::to_string(session_uuid)
        );

    for (const std::string & requested_file_path : requested_files_client_paths) {
        session_confirmed_msg.mutable_session_confirmed()->add_required_files
            (
            requested_file_path
            );
    }
   
    new_session.start_session();
    // We have to construct the response for the client
    // and send it back, before it starts sending file chunks
    co_await proto_io::send_msg(new_session.get_session_socket(), session_confirmed_msg);

}   /* ClientView::add_session() */




