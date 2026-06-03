#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/process/environment.hpp>

#include "unix_ipc_socket.hpp"

static void append_u32(
    std::vector<uint8_t>& out,
    uint32_t value)
{
    out.push_back((value >> 24) & 0xFF);
    out.push_back((value >> 16) & 0xFF);
    out.push_back((value >> 8) & 0xFF);
    out.push_back(value & 0xFF);
}

static uint32_t read_u32(
    const uint8_t* data)
{
    return
        (uint32_t(data[0]) << 24) |
        (uint32_t(data[1]) << 16) |
        (uint32_t(data[2]) << 8) |
        uint32_t(data[3]);
}

static void append_string(
    std::vector<uint8_t>& out,
    const std::string& s)
{
    append_u32(
        out,
        static_cast<uint32_t>(s.size()));

    out.insert(
        out.end(),
        s.begin(),
        s.end());
}

static std::string read_string(
    const std::vector<uint8_t>& data,
    size_t& offset)
{
    uint32_t len =
        read_u32(data.data() + offset);

    offset += 4;

    std::string s(
        reinterpret_cast<const char*>(
            data.data() + offset),
        len);

    offset += len;

    return s;
}

static std::vector<uint8_t>
serialize_request(
    const UnixIpcRequest& req)
{
    std::vector<uint8_t> out;

    append_string(
        out,
        req.current_working_dir);

    append_u32(
        out,
        static_cast<uint32_t>(
            req.cmd_line_args.size()));

    for (const auto& arg :
         req.cmd_line_args)
    {
        append_string(out, arg);
    }

    return out;
}

static UnixIpcResponse
deserialize_response(
    const std::vector<uint8_t>& data)
{
    size_t offset = 0;

    int exit_code =
        static_cast<int>(
            read_u32(data.data() + offset));

    offset += 4;

    std::string stdout_content =
        read_string(data, offset);

    std::string stderr_content =
        read_string(data, offset);

    return UnixIpcResponse{
        exit_code,
        stdout_content,
        stderr_content
    };
}

UnixIpcResponse send_request_to_distbuild
    (
    boost::asio::io_context &   io_ctx,
    const std::vector<std::string> & 
                                cmd_args
    )
{
    const std::string IPC_SOCKET_PATH = "/tmp/distbuild_ipc.sock";
    UnixIpcRequest compilation_request{
        std::filesystem::current_path().string(),
        cmd_args
    };
    
    try {
        std::vector<uint8_t> payload =
            serialize_request(compilation_request);

        std::vector<uint8_t> framed;

        append_u32(
            framed,
            static_cast<uint32_t>(
        payload.size()));

        framed.insert(
            framed.end(),
            payload.begin(),
            payload.end());

        boost::asio::local::stream_protocol::socket socket(io_ctx);

        socket.connect(
            boost::asio::local::stream_protocol::endpoint(
                IPC_SOCKET_PATH));

        boost::asio::write(
            socket,
            boost::asio::buffer(framed));

        std::array<uint8_t, 4> response_size_buf;

        boost::asio::read(
            socket,
            boost::asio::buffer(response_size_buf));

        uint32_t response_size =
            read_u32(response_size_buf.data());

        std::vector<uint8_t> response_payload(
            response_size);

        boost::asio::read(
            socket,
            boost::asio::buffer(response_payload));

        UnixIpcResponse response =
            deserialize_response(
                response_payload);
        
        return response;
    }
    catch (const std::exception & e) {
        return UnixIpcResponse{
            1,
            "",
            "distbuild: " + std::string(e.what())
        };

    }
    
}

int main(int argc, char ** argv ) {
    
    auto start_time = std::chrono::steady_clock::now();

#if 0
    if (argc < 3) {
        std::cerr << "distbuild: Not enough cmd line args\n";
        return 1;
    }
#endif
    
    auto environment_variables = boost::this_process::environment();
    
    boost::asio::io_context io_ctx;
    const char * DISTBUILD_PATH = "DISTBUILD_PATH";
    UnixIpcResponse distbuild_response;

    // if (environment_variables.find(DISTBUILD_PATH) != environment_variables.end()) {
    if (true) {
#if 0
        std::vector<std::string> cmd_args;
        for (int arg_idx = 1; arg_idx < argc; ++arg_idx) {
            cmd_args.emplace_back(argv[arg_idx]);
        }
#endif
        
        std::vector<std::string> cmd_args = {
        "/usr/bin/clang++", "-DBOOST_ATOMIC_DYN_LINK", "-DBOOST_ATOMIC_NO_LIB", "-DBOOST_FILESYSTEM_DYN_LINK", 
        "-g", "-Wall", "-Wextra", "-Wshadow", "-std=c++20", "-MD", "-MT", 
        "CMakeFiles/distbuild_daemon.dir/src/client_session.cpp.o", "-o", 
        "CMakeFiles/distbuild_daemon.dir/src/client_session.cpp.o", "-c", 
        "/home/radu/distbuild/DistBuildSys/src/client_session.cpp"

        };

        distbuild_response = send_request_to_distbuild(io_ctx, cmd_args);        

    }
    else {
        std::cerr << "DISTBUILD_PATH not configured inside environment variables!\n";
        return 1;
    }
    
    
    std::cout << distbuild_response.stdout_content;
    std::cerr << distbuild_response.stderr_content;
    
    auto end_duration = std::chrono::steady_clock::now() - start_time;
    std::cout << "\nTOTAL DURATION: " << std::chrono::duration_cast<std::chrono::milliseconds>(
        end_duration).count() << '\n';

    io_ctx.run();
    return distbuild_response.compiler_exit_code;

}


