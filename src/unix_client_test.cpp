#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <boost/asio.hpp>

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

int main(int argc, char ** argv) {
    
    try {
        if (argc < 2) {
            std::cerr
                << "Usage:\n"
                << argv[0]
                << " [args...]\n";

            return 1;
        }

        std::string socket_path = "/tmp/distbuild_ipc.sock";

        std::vector<std::string> cmd_args;

        for (int i = 1; i < argc; ++i) {
            cmd_args.emplace_back(argv[i]);
        }

        UnixIpcRequest request{
            std::filesystem::current_path().string(),
            cmd_args
        };

        std::vector<uint8_t> payload =
            serialize_request(request);

        std::vector<uint8_t> framed;

        append_u32(
            framed,
            static_cast<uint32_t>(
                payload.size()));

        framed.insert(
            framed.end(),
            payload.begin(),
            payload.end());

        boost::asio::io_context io;

        boost::asio::local::stream_protocol::socket socket(io);

        socket.connect(
            boost::asio::local::stream_protocol::endpoint(
                socket_path));

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

        std::cout
            << "Exit code: "
            << response.compiler_exit_code
            << "\n";

        std::cout
            << "========== STDOUT ==========\n";

        std::cout
            << response.stdout_content
            << "\n";

        std::cout
            << "========== STDERR ==========\n";

        std::cout
            << response.stderr_content
            << "\n";

        return response.compiler_exit_code;
    }
    catch (const std::exception& e) {
        std::cerr
            << "Client error: "
            << e.what()
            << "\n";

        return 1;
    }

}
