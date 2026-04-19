#include "compiler_manager.hpp"

#include <atomic>
#include <boost/process/args.hpp>
#include <chrono>
#include <sstream>

#include <boost/process.hpp>
#include <boost/process/detail/child_decl.hpp>

#include "server_session.hpp"

void CompilerManager::_worker_loop() {
    for (;;) {

        CompilationRequest compile_task{};

        {     
            std::unique_lock<std::mutex> lock(_mtx);
            _cv.wait(lock, [&] {
                return _stop_threads || !_compile_task_queue.empty(); 
            });

            if (_stop_threads && _compile_task_queue.empty()) {
                return;
            }

            compile_task = _compile_task_queue.front();
            _compile_task_queue.pop();
        }
        
        // We need to only allow N source files to be compiled in parallel
        _active_compile_jobs.fetch_add(1, std::memory_order_relaxed); 

        // Call the function which compiles the file
        CompilationOutput compilation_output = _compile_src(compile_task);
        
        _active_compile_jobs.fetch_sub(1, std::memory_order_relaxed);

        // How do I signal the session that the compilation has ended ???
        // I need a server session method which publishes the compilation results
        compile_task.requester_session->publish_compilation_results(compilation_output);

    } 

}   /* CompilerManager::_worker_loop() */


CompilationOutput CompilerManager::_compile_src
    (
    const CompilationRequest & compilation_rqst
    ) 
{   
    boost::process::ipstream stdout_stream;
    boost::process::ipstream stderr_stream;
    
    auto compilation_start_ts = std::chrono::steady_clock::now();
    
    std::cout << "Compilation current dir: " << compilation_rqst.current_working_dir << '\n'; 

    boost::process::child compiler_process
        (
        compilation_rqst.compiler_name,
        boost::process::args(compilation_rqst.cmd_line_args),
        boost::process::std_out > stdout_stream,
        boost::process::std_err > stderr_stream,
        boost::process::start_dir = compilation_rqst.current_working_dir
        );
     
    compiler_process.wait();

    // This is weird and might break
    std::ostringstream stdout_oss;
    stdout_oss << stdout_stream.rdbuf();
    
    std::ostringstream stderr_oss;
    stderr_oss << stderr_stream.rdbuf(); 

    auto compilation_end_ts = std::chrono::steady_clock::now();
    int compilation_duration = std::chrono::duration_cast<std::chrono::seconds>
        (
        compilation_end_ts - compilation_start_ts
        ).count();

    int compiler_exit_code = compiler_process.exit_code();
    
    std::cout << "COMPILER EXIT CODE:" << compiler_exit_code << '\n';
    std::cout << "COMPILATION STDOUT: \n";
    std::cout << stdout_oss.str() << '\n';
    
    std::cout << "COMPILATION STDERR: \n";
    std::cout << stderr_oss.str() << '\n';

    // I have to return something to the server session
    return CompilationOutput{
        stdout_oss.str(),
        stderr_oss.str(),
        compilation_duration,
        compiler_exit_code
    };

}   /* CompilerManager::_compile_src() */

