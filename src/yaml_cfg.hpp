#ifndef YAML_CFG_HPP
#define YAML_CFG_HPP

#include <iostream>
#include <string>

#include "yaml-cpp/yaml.h"

class YamlConfig {

public:
        
    YamlConfig() = default;
    YamlConfig(const YamlConfig &) = delete;
    YamlConfig & operator=(const YamlConfig &) = delete;

    std::string node_uuid;
    std::string node_ip;
    uint32_t    node_main_port;
    uint32_t    node_advertising_port;
    std::string compiler_bin;
    int         compiler_threads;
    
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
            
            node_uuid =
                root["node"]["uuid"].as<std::string>();
            node_ip = 
                root["node"]["ip"].as<std::string>();
            node_main_port = 
                root["node"]["main_port"].as<uint32_t>();
            node_advertising_port =
                root["node"]["advertising_port"].as<uint32_t>();

            compiler_bin =
                root["compiler"]["binary"].as<std::string>();

            compiler_threads = 
                root["compiler"]["threads"].as<int>();
            
            return true;

        }
        catch (const std::exception & e) {
            std::cout << e.what() << '\n';
            return false;
        }
    }    

};

#endif /* YAML_CFG_HPP */

