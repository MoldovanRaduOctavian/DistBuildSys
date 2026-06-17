#ifndef SERVER_SESSION_HPP
#define SERVER_SESSION_HPP

#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include <boost/asio.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "client_view.hpp"
#include "distbuild_messages.pb.h"
#include "file_state_view.hpp"
#include "yaml_cfg.hpp"

class CompilerManager;
struct CompilationOutput;
class Server;

class ServerSession : public std::enable_shared_from_this<ServerSession> {

public:

    ServerSession
        (
        boost::asio::ip::tcp::socket && 
                                session_socket,
        Server *                server,
        ClientView *            parent_client_view,
        CompilerManager *       compiler_manager,
        const boost::uuids::uuid &   
                                session_uuid,
        const distbuild::ClientSessionStartRequest &
                                session_start_rqst
        ) :
        _session_socket(std::move(session_socket)),
        _compiler_manager(compiler_manager),
        _terminated(false),
        _are_args_ready(false),
        _server(server),
        _parent_client_view(parent_client_view),
        _session_uuid(session_uuid),
        _current_working_dir(parent_client_view->get_curr_working_dir()),
        _in_src_file(session_start_rqst.client_source_file()),
        // _compiler_name(session_start_rqst.client_compiler())
        _compiler_name(YamlConfig::instance().compiler_bin)
    {
        std::cout << "THIS IS THE COMPILER: " << _compiler_name << '\n'; 
    };
    
    ~ServerSession() {
        std::cout << "SERVER SESSION DESTROYED: " << _session_uuid << '\n';
        try { 
            boost::system::error_code ec;
            _session_socket.cancel();
            auto ec1 = _session_socket.shutdown(boost::asio::socket_base::shutdown_both, ec);
            _session_socket.close();
        }
        catch (const std::exception & e) {
            // std::cout << e.what() << '\n';
        }
    }

    void add_required_file_state
        (
        FileStateView & file_state
        )
    {
        std::lock_guard<std::mutex> lock(_mtx);
        _required_files_state.push_back(&file_state);
    }

    const std::vector<FileStateView *> &
        get_required_files_state() const
    {
        std::lock_guard<std::mutex> lock(_mtx);
        return _required_files_state;
    }
    
    boost::asio::ip::tcp::socket &
        get_session_socket()
    {
        std::lock_guard<std::mutex> lock(_mtx);
        return _session_socket;
    }
    
    boost::uuids::uuid get_session_uuid() const
    {
        return _session_uuid;    
    }
    
    const std::string & get_curr_working_dir() const
    {
        return _current_working_dir;
    }
    
    bool get_are_args_ready() const {
        return _are_args_ready;
    }

    void start_session()
    {
        auto self = shared_from_this();
        boost::asio::co_spawn
            (
            _session_socket.get_executor(), 
            handle_server_session(self), 
            boost::asio::detached
            );
    }

    boost::asio::awaitable<void> handle_server_session
            (
            std::shared_ptr<ServerSession> self
            );

    // Rewrite file upload chunk handling logic
    bool process_file_chunk
        (
        std::shared_ptr<distbuild::ClientFileChunkUploadRequest>
                    file_chunk_msg
        );

    void preprocess_compiler_call
        (
        const std::string &     client_working_dir,             
        const std::vector<std::string> & 
                                compiler_cmd_line_args,
        const std::vector<std::string> &
                                compiler_idirs
        );
    
    void try_request_src_compilation(bool lock_mtx = false);
    
    void publish_compilation_results
        (
        const CompilationOutput & compilation_out
        );
    
    void send_compilation_result(); 

    void send_msg
        (
        const distbuild::ServerMessage & msg
        );
    
    void set_available_compiler_jobs(size_t available_jobs); 

    void terminate_server_session(); 

private:

    boost::asio::awaitable<void>    _writer_loop(std::shared_ptr<ServerSession> self);

    boost::asio::ip::tcp::socket    _session_socket;
    boost::asio::strand<boost::asio::any_io_executor>
                                    _strand{ _session_socket.get_executor() };
    std::deque<std::string>         _write_queue;
    std::atomic<bool>               _write_in_progress;
    std::atomic<bool>               _terminated;
    std::atomic<bool>               _are_args_ready;

    Server *                        _server;
    CompilerManager *               _compiler_manager;
    ClientView *                    _parent_client_view;
    boost::uuids::uuid              _session_uuid;
    // A server UUID could be added too, but add it later
    
    std::atomic<bool>               _is_compiler_running;

    // Files to be uploaded internal state
    std::vector<FileStateView *>    _required_files_state;    

    std::unordered_map<std::string, FileTransferState>
                                    _file_transfer_states;

    std::string                     _in_src_file;
    std::string                     _compiler_name;
    std::string                     _current_working_dir;
    std::vector<std::string>        _cmd_line_args;
    
    std::string                     _out_obj_file;
    int                             _compiler_exit_code;
    std::string                     _compiler_stdout;
    std::string                     _compiler_stderr;
    int                             _compile_duration;

    uint64_t                        _compile_start_time;
    
    mutable std::mutex              _mtx;

};

#endif /* SERVER_SESSION_HPP */

