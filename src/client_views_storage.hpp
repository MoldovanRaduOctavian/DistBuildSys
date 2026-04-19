#ifndef CLIENT_VIEWS_STORAGE_HPP
#define CLIENT_VIEWS_STORAGE_HPP

#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <string>

#include "client_view.hpp"

/* This class is the owner of all client views */
class ClientViewsStorage {

public:
    
    static constexpr const char * CLIENTS_WORKSPACE = "/tmp/distbuild/client_storage";
    
    /* The returned pointer cannot be const, as a client view needs to be updated */
    /* Throughout its lifetime */
    ClientView * add_client_view
        (
        const std::string & client_uuid
        )
    {
        // Remove the already existent client view and create a new one
        ClientView * curr_client_view = get_client_view(client_uuid);
        if (curr_client_view != nullptr) {
            // If a client view for an UUID already exists
            // then do nothing
            return curr_client_view;
        }
          
        std::filesystem::path client_workspace_path = 
            std::filesystem::path(CLIENTS_WORKSPACE) / client_uuid;
        std::filesystem::create_directories(client_workspace_path);
        
        std::lock_guard<std::mutex> lock(_mtx);
        auto [client_view_it, ok] = _client_views.try_emplace
            (
            client_uuid,
            client_uuid, 
            client_workspace_path.string()
            );
        
        return &client_view_it->second;

    }

    ClientView * get_client_view
        (
        const std::string & client_uuid
        )
    {
        // For each function that only reads the data of the class
        // we could use shared_lock to allow for multiple readers
        // Using lock_guard everywhere is overly pessimistic
        std::lock_guard<std::mutex> lock(_mtx);
        auto client_view_pair = _client_views.find(client_uuid);
        if (client_view_pair == _client_views.end()) {
            return nullptr;
        }

        return &client_view_pair->second;

    }
    
    void remove_client_view
        (
        const std::string & client_uuid
        )
    {
        std::lock_guard<std::mutex> lock(_mtx);
        auto client_view_pair = _client_views.find(client_uuid);
        if (client_view_pair != _client_views.end()) {
            _client_views.erase(client_view_pair);
            // This should also involve deleting the workpsace of the client?
            // The client workspace should stay persistent
        }

    }

private:
    
    /* Mapping from a client uuid to the associtated client view */
    std::unordered_map<std::string, ClientView> 
                            _client_views;
    mutable std::mutex      _mtx;

};

#endif /* CLIENT_VIEWS_STORAGE_HPP */
