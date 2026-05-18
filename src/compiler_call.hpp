#ifndef COMPILER_CALL_HPP
#define COMPILER_CALL_HPP

#include <array>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "includes_rework.hpp"
#include "dep_files.hpp"


class CompilerCall {

public:

    enum class CallType : uint8_t {
        CALL_TYPE_COMPILE,
        CALL_TYPE_LINK,
        CALL_TYPE_COUNT
    };
    

private:
    
    static constexpr std::string_view ARG_DASH_O    = "-o";
    static constexpr std::string_view ARG_DASH_I    = "-I";
    static constexpr std::string_view ARG_DASH_ISYSTEM
                                                    = "-isystem";
    static constexpr std::string_view ARG_DASH_IQUOTE
                                                    = "-iquote";
    static constexpr std::string_view ARG_DASH_INCLUDE
                                                    = "-include";
    static constexpr std::array SUPPORTED_COMPILERS = 
        {
        std::string_view{"/usr/bin/c++"},
        std::string_view{"/usr/bin/clang"},
        std::string_view{"/usr/bin/clang++"},
        std::string_view{"/usr/bin/gcc"},
        std::string_view{"/usr/bin/g++"},
        };

    IncludesCache *     _includes_cache;
    CmdLineIncludeDirs  _cmd_line_include_dirs;
    DepFilesState       _dep_cmd_state;

    std::chrono::steady_clock::time_point
                        _call_creation_time;
    std::chrono::steady_clock::duration
                        _call_resolution_duration;
    CallType            _compiler_call_type;
    std::string         _current_working_dir;
    std::string         _input_src_file;
    std::string         _output_obj_file;
    std::string         _compiler_type;    
    std::vector<std::string>
                        _cmd_line_args;

    std::string         _stdout_content;
    std::string         _stderr_content;
    int                 _exit_code;
    
    mutable std::mutex  _mtx;

public:

    const IncludesCache * get_includes_cache() const {
        std::lock_guard<std::mutex> lock(_mtx);
        return _includes_cache;
    }

    DepFilesState & get_dep_files_state() {
        std::lock_guard<std::mutex> lock(_mtx);
        return _dep_cmd_state;
    }

    const std::chrono::steady_clock::time_point &
            get_call_creation_time() const 
    {
        std::lock_guard<std::mutex> lock(_mtx);
        return _call_creation_time;
    }

    CallType get_compiler_call_type() const {
        std::lock_guard<std::mutex> lock(_mtx);
        return _compiler_call_type;
    }

    const std::string & get_current_working_dir() const { 
        std::lock_guard<std::mutex> lock(_mtx);
        return _current_working_dir;
    }

    const std::string & get_input_src_file() const { 
        std::lock_guard<std::mutex> lock(_mtx);
        return _input_src_file;
    }
   
    const std::string & get_output_obj_file() const { 
        std::lock_guard<std::mutex> lock(_mtx);
        return _output_obj_file;
    }
    
    const std::string & get_compiler_type() const { 
        std::lock_guard<std::mutex> lock(_mtx);
        return _compiler_type;
    }

    const std::vector<std::string> & get_cmd_line_args() const {
        std::lock_guard<std::mutex> lock(_mtx);
        return _cmd_line_args;
    }
    
    const CmdLineIncludeDirs & get_cmd_line_include_dirs() const {
        std::lock_guard<std::mutex> lock(_mtx);
        return _cmd_line_include_dirs;    
    }
    
    void set_stdout_content(const std::string & stdout_content) {
        std::lock_guard<std::mutex> lock(_mtx);
        _stdout_content = stdout_content;
    }

    void set_stderr_content(const std::string & stderr_content) {
        std::lock_guard<std::mutex> lock(_mtx);
        _stderr_content = stderr_content;
    }
   
    void set_exit_code(int exit_code) {
        std::lock_guard<std::mutex> lock(_mtx);
        _exit_code = exit_code;
    }

    void set_call_duration
        (
        const std::chrono::steady_clock::duration & call_resolution_duration
        ) 
    {
        std::lock_guard<std::mutex> lock(_mtx);
        _call_resolution_duration = call_resolution_duration;
    }

    CompilerCall
        (
        IncludesCache &     includes_cache
        ) :
        _includes_cache(&includes_cache),
        _cmd_line_include_dirs(),
        _call_creation_time(std::chrono::steady_clock::now()),
        _call_resolution_duration(),
        _compiler_call_type(CallType::CALL_TYPE_COUNT),
        _current_working_dir(),
        _input_src_file(),
        _compiler_type(),
        _cmd_line_args(),
        _stdout_content(),
        _stderr_content(),
        _exit_code(0)
        {};

    bool initialize_compiler_call
        (
        const std::string &     compiler_call_directory, 
                                                    /* cwd where the compiler was called */
        const std::vector<std::string> &     
                                cmd_line_contents   /* each element of the compiler call */    
        );
    
    std::vector<IncludeInfo> collect_src_file_dependencies
        (
        const CmdLineIncludeDirs & 
                            system_include_dirs
        );

};

#endif /* COMPILER_CALL_HPP */

