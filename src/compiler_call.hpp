#ifndef COMPILER_CALL_HPP
#define COMPILER_CALL_HPP

#include <array>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "includes_rework.hpp"

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
        std::string_view{"clang"},
        std::string_view{"clang++"},
        std::string_view{"gcc"},
        std::string_view{"g++"},
        std::string_view{"/usr/bin/c++"}
        };

    uint64_t            _call_id;
    IncludesCache &     _includes_cache;
    CmdLineIncludeDirs &
                        _cmd_line_include_dirs;

    std::chrono::system_clock::time_point
                        _call_creation_time;
    std::chrono::system_clock::duration
                        _call_resolution_duration;
    CallType            _compiler_call_type;
    std::string         _input_src_file;
    std::string         _output_obj_file;
    std::string         _compiler_type;    
    std::vector<std::string>
                        _cmd_line_args;

    std::string         _stdout_content;
    std::string         _stderr_content;
    int                 _exit_code;

public:
    CompilerCall
        (
        IncludesCache &     includes_cache,
        CmdLineIncludeDirs &
                            cmd_line_include_dirs
        ) :
        _call_id(0),
        _includes_cache(includes_cache),
        _cmd_line_include_dirs(cmd_line_include_dirs),
        _call_creation_time(std::chrono::system_clock::now()),
        _call_resolution_duration(),
        _compiler_call_type(CallType::CALL_TYPE_COUNT),
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

};

#endif /* COMPILER_CALL_HPP */

