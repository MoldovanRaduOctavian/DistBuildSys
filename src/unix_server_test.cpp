#include <iostream>

#include <boost/asio.hpp>

#include "unix_ipc_socket.hpp"

int main() {
    
    boost::asio::io_context io_ctx;

    UnixIpcManager server
        (
        io_ctx,
        "/tmp/distbuild_ipc.sock",
        [](const UnixIpcRequest & request) -> boost::asio::awaitable<UnixIpcResponse> {
            std::cout << "UNIX IPC REQUEST" << '\n';
            std::cout << "CWD: " << request.current_working_dir << '\n';
            
            for (const auto & arg : request.cmd_line_args) {
                    std::cout << "arg: " << arg << '\n';
            }

            co_return UnixIpcResponse
                (
                0, /* exit code */
                "Compilation successful",
                ""
                );

        }
        );

    server.start_ipc_manager();
    io_ctx.run();
    return 0;

}
