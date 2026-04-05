#ifndef COMPILER_MANAGER_HPP
#define COMPILER_MANAGER_HPP

#include <condition_variable>
#include <queue>
#include <thread>
#include <vector>

class ServerSession;

struct CompilationRequest {
    ServerSession * requester_session;
    std::string     compiler_name;
    std::vector<std::string> 
                    cmd_line_args;
    std::string     current_working_dir;
};

struct CompilationOutput {
    std::string     stdout_data;
    std::string     stderr_data;
    int             compiling_duration;
    int             exit_code;

    CompilationOutput
        (
        const std::string & _stdout_data,
        const std::string & _stderr_data,
        int                 _compiling_duration,
        int                 _exit_code
        ) :
        stdout_data(_stdout_data),
        stderr_data(_stderr_data),
        compiling_duration(_compiling_duration),
        exit_code(_exit_code)
    {};
};


class CompilerManager {

public:
     
    CompilerManager
        (
        size_t thread_pool_sz
        ) :
        _thread_pool_sz(thread_pool_sz)
    {};
    
    ~CompilerManager() {
        {
            std::unique_lock<std::mutex> lock(_mtx);     
            _stop_threads = true;
        }

        _cv.notify_all();
        for (auto & worker_thread : _worker_threads) {
            worker_thread.join();
        }
    }
    
    void add_compilation_request
        (
        CompilationRequest && compilation_rqst
        )
    {
        std::unique_lock<std::mutex> lock(_mtx);
        _compile_task_queue.push(compilation_rqst);

    }
    
    size_t get_thread_pool_sz() const {
        std::unique_lock<std::mutex> lock(_mtx);
        return _thread_pool_sz;
    }
    
    size_t get_active_compile_jobs_ct() const {
        return _active_compile_jobs.load(std::memory_order_relaxed);
    }

private:
    
    void    _worker_loop();
    CompilationOutput 
            _compile_src(const CompilationRequest & compilation_rqst);

    size_t          _thread_pool_sz;
    std::atomic<size_t>          
                    _active_compile_jobs{0};

    std::vector<std::thread> 
                    _worker_threads;
    
    std::queue<CompilationRequest>
                    _compile_task_queue;

    mutable std::mutex      
                    _mtx;
    std::condition_variable 
                    _cv;

    bool            _stop_threads{false};

};

#endif /* COMPILER_MANAGER_HPP */

