#include <algorithm>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/address.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/version.hpp>

#include "spdlog/spdlog.h"

using boost::asio::local::stream_protocol;

#define MAX_PATH_SIZE   (4096)
#define MAX_RQST_SIZE   (1 << 16)
#define IPC_SOCKET      ("/tmp/distbuild_ipc_skt")

static std::vector<std::string> parse_args
    (
    int     argc,
    char ** argv
    )
{
    std::vector<std::string> cmd_line_args;
    for (int i = 0; i < argc; ++i) {
        cmd_line_args.emplace_back(argv[i]);
    }

    return cmd_line_args;

}


// Works on Linux only
// Find the matching call for Windows too
static void call_compiler_locally(char ** argv) {
    execvp(argv[1], argv + 1);
    std::cout << "Something went wrong!\n";
    exit(1);
}


static bool check_linker_call
    (
    const std::vector<std::string> & argv
    )
{
    // If multiple object files are involved
    // then we probably call the linker
    size_t obj_count = std::count_if(argv.begin(), argv.end(), [](const std::string & arg) {
            return arg.ends_with(".o") || arg.ends_with(".so");
        }
    );

    return obj_count > 1;

}


static bool send_compiler_call
    ( 
    int                         argc,
    char **                     argv,
    stream_protocol::socket & 
                                ipc_skt
    )
{
    char cwd_buffer[MAX_PATH_SIZE] = {0};
    if (getcwd(cwd_buffer, MAX_PATH_SIZE) == nullptr) {
        std::cout << "Something bad happened!\n";
        call_compiler_locally(argv);
        return false;
    }
    
    const char record_separator = 0x1e;
    std::string cc_rqst = cwd_buffer;
    cc_rqst += record_separator;
    for (size_t i = 1; i < argc; ++i) {
        cc_rqst += argv[i];
        cc_rqst += record_separator;
    }
    cc_rqst += '\n';
    std::cout << "SENT MESSAGE: " << cc_rqst;

    if (cc_rqst.size() > MAX_RQST_SIZE) {
        std::cout << "Something bad happened!\n";
        call_compiler_locally(argv);
        return false;
    }

    boost::system::error_code error;
    boost::asio::write(ipc_skt, boost::asio::buffer(cc_rqst), error);
    if (error) {
        std::cout << "Something bad happened!\n";
        call_compiler_locally(argv);
        return false;
    }
    
    return true;

}


boost::asio::awaitable<void> listen_adv
    (
    boost::asio::ip::udp::socket & socket
    ) 
{
    std::array<char, 1500>          buff;
    boost::asio::ip::udp::endpoint  broadcast_endpoint;
    boost::asio::steady_timer       broadcast_timer(socket.get_executor());

    for (;;) {
        broadcast_timer.expires_after(std::chrono::milliseconds(100));
        size_t num_bytes = co_await socket.async_receive_from(
            boost::asio::buffer(buff),
            broadcast_endpoint,
            boost::asio::use_awaitable
        );
        
        co_await broadcast_timer.async_wait(boost::asio::use_awaitable);
        std::cout << "Received broadcast: " << std::string(buff.data()) << '\n';
    }

}

int main
    (
    int     argc, 
    char ** argv
    ) 
{
    /* boost::asio::io_context io_ctx;
    stream_protocol::socket ipc_skt(io_ctx);
    
    struct FileDeleter {
        const char * _file;
        FileDeleter(const char * file) : _file(file) {};
        ~FileDeleter() {
            unlink(_file);
        }
    };
    auto deleter = FileDeleter(IPC_SOCKET);

    const std::vector<std::string> argv_vec = parse_args(argc, argv);
    if (check_linker_call(argv_vec)) {
        // Trigger compilation locally
        std::cout << "Linker call!!!\n";
        call_compiler_locally(argv);
        return 0;
    }

    // I have to check for socket connection errors
    boost::system::error_code error;
    ipc_skt.connect(stream_protocol::endpoint(IPC_SOCKET), error);
    if (error) {
        // Trigger compilationn locally
        call_compiler_locally(argv);
        return 0;
    }
    
    // This call is blocking, just like a normal compiler call
    if (false == send_compiler_call(argc, argv, ipc_skt)) {
        call_compiler_locally(argv);
    }

    // Receive the output of the compilation process
    */
    
    boost::asio::io_context         io_ctx;
    boost::asio::ip::udp::socket    recv_skt(
        io_ctx, 
        boost::asio::ip::udp::endpoint(
            boost::asio::ip::udp::v4(),
            5000
        )
    );
    
    recv_skt.set_option(boost::asio::socket_base::reuse_address(true));
    recv_skt.set_option(boost::asio::socket_base::broadcast(true));
    boost::asio::co_spawn(io_ctx, listen_adv(recv_skt), boost::asio::detached); 
    io_ctx.run();
    return 0;

}

