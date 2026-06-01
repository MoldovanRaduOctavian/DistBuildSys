#include "compiler_call.hpp"

#include <chrono>
#include <iostream>
#include <mutex>
#include <ranges>

#include "includes_rework.hpp"

bool CompilerCall::initialize_compiler_call
    (
    const std::string &     compiler_call_directory, 
                                                /* cwd where the compiler was called */
    const std::vector<std::string> &     
                            cmd_line_contents   /* each element of the compiler call */    
    )
{   
    std::lock_guard<std::mutex> lock(_mtx);
    _call_creation_time = std::chrono::steady_clock::now();
    _current_working_dir = compiler_call_directory;
    
    auto get_next_arg = [&](const size_t idx) -> std::pair<bool, std::string> {
        if (idx + 1 < cmd_line_contents.size()) {
            return std::make_pair(true, cmd_line_contents[idx + 1]);
        }

        return std::make_pair(false, "");
    };
    
    auto is_header_file = [](const std::string & file_name) -> bool {
        if (file_name.ends_with(".h")
         || file_name.ends_with(".hpp")
         || file_name.ends_with(".hh")
         || file_name.ends_with(".inc")
        )
        {
            return true;
        }

        return false;

    };

    auto is_src_file = [](const std::string & file_name) -> bool {
        if (file_name.ends_with(".c")
         || file_name.ends_with(".cc")
         || file_name.ends_with(".cpp")
        )
        {
            return true;
        }

        return false;
       
    };

    auto is_for_linking = [](const std::string & file_name) -> bool {
        if (file_name.ends_with(".o")
         || file_name.ends_with(".a")
         || file_name.ends_with(".so")
         || file_name.ends_with(".dll")
        )
        {
            return true;
        }

        return false;
       
    };

    auto get_abs_path = 
    [](const std::string & current_dir, const std::string & path) -> std::string {
        return path.starts_with('/') ? path : current_dir + "/" + path;
    };

    for (size_t arg_idx = 0; arg_idx < cmd_line_contents.size(); ++arg_idx) {
        if (arg_idx == 0) {
            if (std::ranges::find
                (SUPPORTED_COMPILERS.begin(), 
                 SUPPORTED_COMPILERS.end(),
                 cmd_line_contents[arg_idx]
                 ) == SUPPORTED_COMPILERS.end())
            {
                return false;
            }

            _compiler_type = cmd_line_contents[arg_idx];
        }
        else if (cmd_line_contents[arg_idx].starts_with('-')) {

            _cmd_line_args.emplace_back(cmd_line_contents[arg_idx]);

            if (cmd_line_contents[arg_idx] == ARG_DASH_I) {
                const auto & [status, next_arg] = get_next_arg(arg_idx);
                if (status) {
                    auto dash_i_path = 
                        get_abs_path(compiler_call_directory, next_arg);
                    _cmd_line_include_dirs.dash_i.emplace_back(
                        dash_i_path
                    );
                    _cmd_line_args.emplace_back(dash_i_path);

                    ++arg_idx;
                }
                else {
                    _compiler_call_type = CallType::CALL_TYPE_COUNT;
                    return false;
                }
            }
            else if (cmd_line_contents[arg_idx] == ARG_DASH_ISYSTEM) {
                const auto & [status, next_arg] = get_next_arg(arg_idx);
                if (status) {
                    auto dash_isystem_path =
                        get_abs_path(compiler_call_directory, next_arg);
                    _cmd_line_include_dirs.dash_isystem.emplace_back(
                        dash_isystem_path
                    );
                    _cmd_line_args.emplace_back(dash_isystem_path);
                    ++arg_idx;
                }
                else {
                    _compiler_call_type = CallType::CALL_TYPE_COUNT;
                    return false;
                }
            }
            else if (cmd_line_contents[arg_idx] == ARG_DASH_IQUOTE) {
                const auto & [status, next_arg] = get_next_arg(arg_idx);
                if (status) {
                    auto dash_iquote_path = 
                        get_abs_path(compiler_call_directory, next_arg);
                    _cmd_line_include_dirs.dash_iquote.emplace_back(
                        dash_iquote_path
                    );
                    _cmd_line_args.emplace_back(dash_iquote_path);
                    ++arg_idx;
                }
                else {
                    _compiler_call_type = CallType::CALL_TYPE_COUNT;
                    return false;
                }
            }
            else if (cmd_line_contents[arg_idx] == ARG_DASH_INCLUDE) {
                const auto & [status, next_arg] = get_next_arg(arg_idx);
                if (status) {
                    auto dash_include_path = 
                        get_abs_path(compiler_call_directory, next_arg);
                    _cmd_line_include_dirs.dash_include.emplace_back(
                        dash_include_path
                    );
                    _cmd_line_args.emplace_back(dash_include_path);
                    ++arg_idx;
                }
                else {
                    _compiler_call_type = CallType::CALL_TYPE_COUNT;
                    return false;
                }
            }
            else if (cmd_line_contents[arg_idx] == ARG_DASH_O) {
                const auto & [status, next_arg] = get_next_arg(arg_idx);
                if (status) {
                    _output_obj_file = next_arg;
                    _cmd_line_args.emplace_back(next_arg);
                    ++arg_idx;
                }
                else {
                    _compiler_call_type = CallType::CALL_TYPE_COUNT;
                    return false;
                }
            }
            else if (cmd_line_contents[arg_idx] == "-MF") {
                const auto & [status, next_arg] = get_next_arg(arg_idx);
                if (status) {
                    auto mf_flag_path = 
                        get_abs_path(compiler_call_directory, next_arg);
                    _dep_cmd_state.add_mf_flag_path(
                        mf_flag_path
                    );
                    _cmd_line_args.emplace_back(mf_flag_path);
                    ++arg_idx;
                }
                else {
                    _compiler_call_type = CallType::CALL_TYPE_COUNT;
                    return false;
                }
            }
            else if (cmd_line_contents[arg_idx] == "-MT") {
                const auto & [status, next_arg] = get_next_arg(arg_idx);
                if (status) {
                    _dep_cmd_state.add_mt_flag_path(next_arg);
                    _cmd_line_args.emplace_back(next_arg);
                    ++arg_idx;
                }
                else {
                    _compiler_call_type = CallType::CALL_TYPE_COUNT;
                    return false;
                }
            }
            else if (cmd_line_contents[arg_idx] == "-MQ") {
                const auto & [status, next_arg] = get_next_arg(arg_idx);
                if (status) {
                    _dep_cmd_state.add_mq_flag_path(next_arg);
                    _cmd_line_args.emplace_back(next_arg);
                    ++arg_idx;
                }
                else {
                    _compiler_call_type = CallType::CALL_TYPE_COUNT;
                    return false;
                }
            }
            else if (cmd_line_contents[arg_idx] == "-MD") {
                _dep_cmd_state.set_md_flag_present(true);
            }
            else if (cmd_line_contents[arg_idx] == "-MMD") {
                _dep_cmd_state.set_mmd_flag_present(true);
            }
            else if (cmd_line_contents[arg_idx] == "-MP") {
                _dep_cmd_state.set_mp_flag_present(true);
            }

        }
        else if (is_src_file(cmd_line_contents[arg_idx])
              || is_header_file(cmd_line_contents[arg_idx])
        )
        {
            _input_src_file = cmd_line_contents[arg_idx]; 
            _cmd_line_args.emplace_back(cmd_line_contents[arg_idx]);
        }
        else if (is_for_linking(cmd_line_contents[arg_idx])) 
        {
            _compiler_call_type = CallType::CALL_TYPE_LINK;
            _cmd_line_args.emplace_back(cmd_line_contents[arg_idx]);
        }
        else {
            _cmd_line_args.emplace_back(cmd_line_contents[arg_idx]);
        }
        
    }
    
    if (_output_obj_file.ends_with(".o")) {
        _compiler_call_type = CallType::CALL_TYPE_COMPILE;
    }  

    if (_input_src_file.empty()) {
        return false;
    }
    
    if (_compiler_call_type == CallType::CALL_TYPE_COUNT) {
        return false;
    }

    return true;

}   /* CompilerCall::initialize_compiler_call() */


std::vector<IncludeInfo> CompilerCall::collect_src_file_dependencies
    (
    const CmdLineIncludeDirs & 
                        system_include_dirs
    )
{   
    _cmd_line_include_dirs.append_include_dirs(system_include_dirs);
    SourceFileParser src_file_parser(_cmd_line_include_dirs, *_includes_cache); 
    
    if ( src_file_parser.parse_source_file_includes
        (
        _input_src_file,
        _cmd_line_include_dirs.dash_i
        ) == false ) {
        std::cout << "PARSING A SOURCE FILE INCLUDE HAS FAILED\n";
        return std::vector<IncludeInfo>{};
    }
    
    std::unordered_map<std::string, IncludeInfo> &
        parser_includes_map = src_file_parser.get_include_directives();     

    std::vector<IncludeInfo> include_dependencies;
    include_dependencies.reserve(parser_includes_map.size());
    for (auto & [_, include_info] : parser_includes_map) {
        include_dependencies.push_back(std::move(include_info));
    }    
    
    return include_dependencies;    

}   /* CompilerCall::collect_src_file_dependencies() */


