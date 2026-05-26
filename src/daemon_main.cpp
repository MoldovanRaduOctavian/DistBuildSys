#include <boost/asio/io_service.hpp>
#include <chrono>
#include <iostream>

#include "includes_rework.hpp"
#include "compiler_call.hpp"
#include "client.hpp"
#include "yaml_cfg.hpp"

#include <boost/asio.hpp>

#define IPC_SOCKET ("/tmp/distbuild_ipc_skt")

class ResourceAdvertisementService {

    static constexpr const char *   BROADCAST_ADDR      = "255.255.255.255";
    static constexpr uint16_t       ADVERTISEMENT_PORT  = 5000;
    static constexpr const char *   DUMMY_MSG           = "compile jobs available\n\0";
    
    boost::asio::io_context &       _io_ctx;
    boost::asio::ip::udp::socket    _udp_skt;
    
    boost::asio::steady_timer       _broadcast_timer;
    boost::asio::ip::udp::endpoint  _broadcast_endpoint;

public:
    ResourceAdvertisementService(
        boost::asio::io_context & io_ctx
        ) :
        _io_ctx(io_ctx),
        _udp_skt(_io_ctx),
        _broadcast_timer(io_ctx),
        _broadcast_endpoint(
            boost::asio::ip::address::from_string(BROADCAST_ADDR),
            ADVERTISEMENT_PORT
        )
    {
        _udp_skt.open(boost::asio::ip::udp::v4());
        _udp_skt.set_option(boost::asio::socket_base::reuse_address(true));
        _udp_skt.set_option(boost::asio::socket_base::broadcast(true));        
        _send_adv_periodic();
    };

private:
    void _send_adv_periodic() {
        _broadcast_timer.expires_after(std::chrono::seconds(1));
        _broadcast_timer.async_wait([this](const boost::system::error_code & error) {
            if (!error) {
                _send_pkt_helper();
            }
        });
    }

    void _send_pkt_helper() {
        _udp_skt.async_send_to(
            boost::asio::buffer(std::string(DUMMY_MSG)),
            _broadcast_endpoint,
            [this](const boost::system::error_code & error, size_t num_bytes) {
                if (!error) {
                    std::cout << "PACKET GOT SENT\n";
                    _send_adv_periodic();
                }
            }
        );
    }
};


int main() {
    boost::asio::io_context io_ctx;
    // auto                    res_adv = 
    //     ResourceAdvertisementService(io_ctx);
        
    // CmdLineIncludeDirs  cmd_line_includes_1{"clang"};
    // IncludesCache       includes_cache;
    
    /*
    auto cold_cache_start = std::chrono::high_resolution_clock::now();
    auto src_file_parser_1 = SourceFileParser(cmd_line_includes_1, includes_cache);
    bool status = src_file_parser_1.parse_source_file_includes(
        "/home/radu/distbuild/DistBuildSys/src/includes_rework.cpp",
        cmd_line_includes_1.dash_include 
    );
    auto cold_cache_duration = std::chrono::high_resolution_clock::now()
        - cold_cache_start; 

    CmdLineIncludeDirs  cmd_line_includes_2{"clang"};
    
    auto hot_cache_start = std::chrono::high_resolution_clock::now();
    auto src_file_parser_2 = SourceFileParser(cmd_line_includes_2, includes_cache);
    status = src_file_parser_2.parse_source_file_includes(
        "/home/radu/distbuild/DistBuildSys/src/includes_rework.cpp",
        cmd_line_includes_2.dash_include 
    );
    auto hot_cache_duration = std::chrono::high_resolution_clock::now()
        - hot_cache_start; 
    
    std::cout << "Cold cache duration: " 
              << std::chrono::duration_cast<std::chrono::milliseconds>(cold_cache_duration) << '\n';

    std::cout << "Hot cache duration: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(hot_cache_duration) << '\n';
    */    

    // I suppose this works well enough for now
    YamlConfig yaml_cfg;    
    auto client = Client(io_ctx);
    auto includes_cache = IncludesCache(client.get_system_include_dirs());    
    auto compiler_call = CompilerCall(includes_cache);
     
    compiler_call.initialize_compiler_call
        (
        "/home/radu/distbuild/artifacts", 
        {"/usr/bin/clang++", "-DBOOST_ATOMIC_DYN_LINK", "-DBOOST_ATOMIC_NO_LIB", "-DBOOST_FILESYSTEM_DYN_LINK", 
        "-g", "-Wall", "-Wextra", "-Wshadow", "-std=c++20", "-MD", "-MT", 
        "CMakeFiles/distbuild_daemon.dir/src/client_session.cpp.o", "-o", 
        "CMakeFiles/distbuild_daemon.dir/src/client_session.cpp.o", "-c", 
        "/home/radu/distbuild/DistBuildSys/src/client_session.cpp"}
        );
     
    auto cold_cache_start = std::chrono::high_resolution_clock::now();

    std::string dep_file_path = client.test_handle_compiler_call(compiler_call);
    // std::cout << dep_file_path << '\n';

    auto cold_cache_duration = std::chrono::high_resolution_clock::now()
        - cold_cache_start;

    std::cout << "Cold cache duration: " 
              << std::chrono::duration_cast<std::chrono::milliseconds>(cold_cache_duration) << '\n';

    auto hot_cache_start = std::chrono::high_resolution_clock::now();

    client.test_handle_compiler_call(compiler_call);

    auto hot_cache_duration = std::chrono::high_resolution_clock::now()
        - hot_cache_start;

    std::cout << "Hot cache duration: " 
              << std::chrono::duration_cast<std::chrono::milliseconds>(hot_cache_duration) << '\n';



    // So the caching does not work at all, fucking great
    // Debug that next, otherwise I think the detected
    // dependencies should be alright
    

    // io_ctx.run();
    return 0;
}
