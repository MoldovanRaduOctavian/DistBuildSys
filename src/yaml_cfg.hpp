#ifndef YAML_CFG_HPP
#define YAML_CFG_HPP

#include <iostream>
#include <string>
#include <vector>

#include "yaml-cpp/yaml.h"

class YamlConfig {

public:
    
    struct Server {
        std::string ip;
        int port;

        Server
            (
            const std::string & _ip, 
            int _port
            ) :
        ip(_ip),
        port(_port)
        {};
    };
    
    YamlConfig() = default;
    YamlConfig(const YamlConfig &) = delete;
    YamlConfig & operator=(const YamlConfig &) = delete;


    std::string compiler_bin;
    int         compiler_threads;
    std::vector<Server>
                servers;
    
    static YamlConfig & instance() {
        static YamlConfig cfg_instance;
        return cfg_instance;
    }

    bool load_cfg
        (
        const std::string & cfg_path
        ) 
    {
        try {
            YAML::Node root = YAML::LoadFile(cfg_path);

            compiler_bin =
                root["compiler"]["binary"].as<std::string>();

            compiler_threads = 
                root["compiler"]["threads"].as<int>();
            
            servers.clear();
            for (const auto & node : root["servers"]) {
                servers.emplace_back(
                    node["ip"].as<std::string>(),
                    node["port"].as<int>()
                );                
            }

        }
        catch (const std::exception & e) {
            std::cout << e.what() << '\n';
            return false;
        }
    }    

};

#endif /* YAML_CFG_HPP */

