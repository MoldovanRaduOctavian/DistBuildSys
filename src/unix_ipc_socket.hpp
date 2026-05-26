#ifndef UNIX_IPC_SOCKET_HPP
#define UNIX_IPC_SOCKET_HPP

#include <atomic>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>

struct UnixIpcRequest {
    std::string              current_working_dir;
    std::vector<std::string> cmd_line_args;
    
    UnixIpcRequest
        (
        const std::string & _current_working_dir,
        const std::vector<std::string> &
                            _cmd_line_args
        ) :
        current_working_dir(_current_working_dir),
        cmd_line_args(_cmd_line_args)
        {};
    
};


struct UnixIpcResponse {
    int         compiler_exit_code;
    std::string stdout_content;
    std::string stderr_content;
    
    UnixIpcResponse() = default;
    UnixIpcResponse
        (
        int                 _compiler_exit_code,
        const std::string & _stdout_content,
        const std::string & _stderr_content
        ) :
        compiler_exit_code(_compiler_exit_code),
        stdout_content(_stdout_content),
        stderr_content(_stderr_content)
        {};
    
    UnixIpcResponse(const UnixIpcResponse&) = default;      
    UnixIpcResponse(UnixIpcResponse&&) = default;   
    UnixIpcResponse& operator=(UnixIpcResponse&&) = default; 

};


class UnixIpcManager {

public:
    
    using RequestHandler =
        std::function<boost::asio::awaitable<UnixIpcResponse>(const UnixIpcRequest &)>;

private:

    boost::asio::io_context &   _io_ctx;
    boost::asio::local::stream_protocol::acceptor
                                _ipc_acceptor;
        
    std::string                 _ipc_socket_path;

    boost::asio::thread_pool    _worker_pool;
    std::atomic<uint32_t>       _active_connections;
    
    RequestHandler              _request_handler;

public:
    
    UnixIpcManager
        (
        boost::asio::io_context & 
                                io_ctx,
        const std::string &     ipc_socket_path,
        RequestHandler          request_handler
        ) :
        _io_ctx(io_ctx),
        _ipc_acceptor(io_ctx),
        _ipc_socket_path(ipc_socket_path),
        _worker_pool(std::thread::hardware_concurrency()),
        _active_connections(0),
        _request_handler(request_handler)
        {};
    
    void start_ipc_manager() {
        std::filesystem::remove(_ipc_socket_path);
        boost::asio::local::stream_protocol::endpoint
            _ipc_endpoint(_ipc_socket_path);

        _ipc_acceptor.open(_ipc_endpoint.protocol());
        _ipc_acceptor.bind(_ipc_endpoint);
        _ipc_acceptor.listen
            (
            boost::asio::socket_base::max_listen_connections
            );
        
        boost::asio::co_spawn
            (
            _io_ctx,
            _accept_loop(),
            boost::asio::detached
            );

    }


    ~UnixIpcManager() {
        _stop_ipc_manager();
    }

private:
    
    void _stop_ipc_manager() {
        boost::system::error_code error_code;
        auto ec = _ipc_acceptor.close(error_code);
        _worker_pool.join();
        std::filesystem::remove(_ipc_socket_path);
    }

    boost::asio::awaitable<void> _accept_loop() {
        while (_ipc_acceptor.is_open()) {
            boost::system::error_code error_code;
            boost::asio::local::stream_protocol::socket connection_socket =
                co_await _ipc_acceptor.async_accept
                    (
                    boost::asio::redirect_error(
                        boost::asio::use_awaitable,
                        error_code
                    ));

            if (error_code) {
                if (_ipc_acceptor.is_open()) {
                    std::cout << "IPC accept error: "
                            << error_code.message()
                            << '\n';
                }

                continue;
            }
            
            boost::asio::co_spawn
                (
                _io_ctx,
                _handle_session(std::move(connection_socket)),
                boost::asio::detached
                );

        }
    }
            
    boost::asio::awaitable<void> _handle_session
        (
        boost::asio::local::stream_protocol::socket connection_socket
        )
    {   
        _active_connections++;

        try {
            std::array<uint8_t, 4> size_buf;
            co_await boost::asio::async_read
                (
                connection_socket,
                boost::asio::buffer(size_buf),
                boost::asio::use_awaitable
                );
            
            uint32_t payload_size = _read_u32(size_buf.data());
            std::vector<uint8_t> payload(payload_size);

            co_await boost::asio::async_read
                (
                connection_socket,
                boost::asio::buffer(payload),
                boost::asio::use_awaitable
                );
            
            UnixIpcRequest request =
                _deserialize_request(payload);
            
            UnixIpcResponse response =
                co_await boost::asio::co_spawn(
                    _worker_pool,
                    _request_handler(request),
                    boost::asio::use_awaitable
                );

            std::vector<uint8_t> response_data =
                _serialize_response(response);
            
            std::vector<uint8_t> framed;
            _append_u32
                (
                framed, 
                static_cast<uint32_t>(response_data.size())
                );
            
            framed.insert
                (
                framed.end(),
                response_data.begin(),
                response_data.end()
                );
            
            co_await boost::asio::async_write
                (
                connection_socket,
                boost::asio::buffer(framed),
                boost::asio::use_awaitable
                );

        }
        catch (const std::exception & e) {
            std::cout << "Session error: "
                    << e.what()
                    << '\n';
        }

        boost::system::error_code error_code;
        auto ec1 = connection_socket.shutdown
            (
            boost::asio::socket_base::shutdown_both,
            error_code
            );
        auto ec2 = connection_socket.close(error_code);
        
        _active_connections--;

    }
    
        
    void _append_u32
        (
        std::vector<uint8_t> &  out,
        uint32_t                value
        ) const
    {
        out.push_back((value >> 24) & 0xFF);
        out.push_back((value >> 16) & 0xFF);
        out.push_back((value >> 8) & 0xFF);
        out.push_back(value & 0xFF);
    }

    uint32_t _read_u32
        (
        const uint8_t * data
        )
    {
        return 
            (uint32_t(data[0]) << 24) |
            (uint32_t(data[1]) << 16) |
            (uint32_t(data[2]) << 8) |
            (uint32_t(data[3]));
    }
        
    void _append_string
        (
        std::vector<uint8_t> &  out,
        const std::string &     s
        )
    {
        _append_u32(
            out,
            static_cast<uint32_t>(s.size()));

        out.insert(
            out.end(),
            s.begin(),
            s.end());

    }
    
    std::string _read_string
        (
        const std::vector<uint8_t> &
                        data,
        size_t &        offset
        )
    {
        uint32_t len =
            _read_u32(data.data() + offset);

        offset += 4;

        std::string s(
            reinterpret_cast<const char*>(
                data.data() + offset),
            len);

        offset += len;
        return s;

    }
    
    std::vector<uint8_t> _serialize_request
        (
        const UnixIpcRequest & req
        )
    {
        std::vector<uint8_t> out;

        _append_string(
            out,
            req.current_working_dir);

        _append_u32(
            out,
            static_cast<uint32_t>(
                req.cmd_line_args.size()));

        for (const auto& arg :
             req.cmd_line_args)
        {
            _append_string(out, arg);
        }

        return out;
    }

    UnixIpcRequest _deserialize_request
        (
        const std::vector<uint8_t> & data
        )
    {
        size_t offset = 0;

        std::string cwd =
            _read_string(data, offset);

        uint32_t argc =
            _read_u32(data.data() + offset);

        offset += 4;

        std::vector<std::string> args;

        for (uint32_t i = 0; i < argc; ++i) {
            args.push_back(_read_string(data, offset));
        }

        return UnixIpcRequest(cwd, args);

    }
    
    std::vector<uint8_t> _serialize_response
        (
        const UnixIpcResponse & resp
        )
    {
        std::vector<uint8_t> out;

        _append_u32(
            out,
            static_cast<uint32_t>(
                resp.compiler_exit_code));

        _append_string(
            out,
            resp.stdout_content);

        _append_string(
            out,
            resp.stderr_content);

        return out;

    }

};


#endif /* UNIX_IPC_SOCKET_HPP */

