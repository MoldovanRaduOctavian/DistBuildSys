#include "client_view.hpp"

#include <filesystem>
#include <mutex>
#include <unordered_set>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "distbuild_messages.pb.h"
#include "file_state_view.hpp"
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
    // I can't lock the mutex in here, lovely
    // std::lock_guard<std::mutex> lock(_mtx);
    auto file_state_pair = _client_file_states.find(client_path);
    if (file_state_pair != _client_file_states.end()
        && file_state_pair->second.file_sha1 == file_sha1
        && file_state_pair->second.file_sz_bytes == file_sz_bytes
        )
    {
        return file_state_pair->second;
    }
    
    
    std::string server_file_path = 
        cnvt_client_to_server_path(_current_working_dir, client_path);

    // Add a new FileStateView 
    // Horrible variable naming
    auto [file_state_it, ok] = _client_file_states.try_emplace
        (
        client_path,
        file_sz_bytes,          
        file_sha1,
        server_file_path
        );
    
    return file_state_it->second;

}   /* ClientView::add_file_state() */


void ClientView::add_session
    (
    boost::asio::ip::tcp::socket
                        session_socket,
    const distbuild::ClientSessionStartRequest &
                        session_start_msg,
    Server *            server,
    CompilerManager *   compiler_manager
    )
{
    boost::uuids::uuid session_uuid = boost::uuids::random_generator()();
    
    {
        std::lock_guard<std::mutex> lock(_mtx);
        auto [session_it, ok] = _associated_sessions.try_emplace
            (
            boost::uuids::to_string(session_uuid),
            std::make_shared<ServerSession>
                (
                std::move(session_socket),
                server,
                this,
                compiler_manager,
                session_uuid,
                session_start_msg
                )
            );
       
        for (const distbuild::FileInfo & file_info : session_start_msg.required_files_info()) {
            // The client view holds a reference to all the files, from sessions
            if (!file_info.filename().empty()) {
                FileStateView & new_file_state = 
                    add_file_state(file_info.filename(), file_info.filesize(), file_info.filehash()); 
                session_it->second->add_required_file_state(new_file_state);

            } 

        }
    
    }
    
    ServerSession & new_session = *_associated_sessions.find(boost::uuids::to_string(session_uuid))->second;

    const std::vector<std::string> client_cmd_line_args
        {
        session_start_msg.client_cmd_line_args().begin(),
        session_start_msg.client_cmd_line_args().end()
        };
    
    const std::vector<std::string> client_idirs
        {
        session_start_msg.client_idirs().begin(),
        session_start_msg.client_idirs().end()
        };

    new_session.preprocess_compiler_call
        (
        session_start_msg.client_working_dir(),
        client_cmd_line_args,
        client_idirs
        );
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
                    cnvt_server_to_client_path(_current_working_dir, updated_file_state.server_file_path);

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
                    cnvt_server_to_client_path(_current_working_dir, file_info->server_file_path)
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
    
    // We should not request a file if it is a usr lib system file
    // std::cout << "requested_files_client_paths size: " << requested_files_client_paths.size() << '\n';
    new_session.send_msg(session_confirmed_msg);         
    new_session.start_session();   
    try_compile_for_active_sessions();

}   /* ClientView::add_session() */


void ClientView::remove_session
    (
    const boost::uuids::uuid & session_uuid
    )
{
    std::lock_guard<std::mutex> lock(_mtx);
    const std::string session_uuid_str = boost::uuids::to_string(session_uuid);
    if (_associated_sessions.find(session_uuid_str) != _associated_sessions.end()) {
        _associated_sessions.erase(session_uuid_str);
    }

}   /* ClientView::remove_session() */



void ClientView::try_compile_for_active_sessions
    (
    bool lock_session_mtx
    ) 
{
    
    std::lock_guard<std::mutex> lock(_mtx);
    for (auto & [_, session] : _associated_sessions) {
        session->try_request_src_compilation(lock_session_mtx);
    }

}   /* ClientView::try_compile_for_active_sessions() */


