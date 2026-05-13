#include "dep_files.hpp"

#include <iostream>

#include "compiler_call.hpp"

std::string DepFilesState::generate_dep_file
    (
    const CompilerCall &    compiler_call,
    const std::vector<const IncludeInfo *>
                            headers
    ) const
{
    std::string target_name = _mt_flag_paths;
    if (target_name.empty() == true) {
        target_name = compiler_call.get_output_obj_file();
    }
    
    std::string dep_file_path = _create_depfile_name(compiler_call);
    const std::vector<std::string> deps_list_root = 
        _calculate_deps_for_headers
            (
            compiler_call,
            headers
            );

    DepFile dep_file;
    dep_file.dep_targets.emplace_back(target_name, deps_list_root);
    if (_mp_flag_present == true) {
        bool is_first = true;
        for (const std::string & dep_list_item : deps_list_root) {
            if (is_first == false) {
                dep_file.dep_targets.emplace_back
                    (
                    escape_gnu_make_spaces(dep_list_item),
                    std::vector<std::string>{}
                    );
            }

            is_first = false;
        }
    }

    // Write dep file to file    
    dep_file.write_to_file(dep_file_path);
    return dep_file_path;

}   /* DepFileState::generate_dep_file() */


const std::vector<std::string> DepFilesState::_calculate_deps_for_headers
    (
    const CompilerCall &    compiler_call,
    const std::vector<const IncludeInfo *>
                            headers
    ) const
{
    // This is where we left off
    std::vector<std::string> dep_file_names;
    std::vector<const IncludeInfo *> filtered_headers;
    if (_mmd_flag_present == true) {
        filtered_headers = 
            _ignore_system_headers
                (
                compiler_call.get_includes_cache()->get_sys_include_dirs(),
                headers
                );
    }
        
    dep_file_names.emplace_back(_process_makefile_target(compiler_call.get_input_src_file()));
    if (_mmd_flag_present == true) {
        for (const IncludeInfo * header : filtered_headers) {
            dep_file_names.emplace_back(_process_makefile_target(header->file_path));
        }
    }
    else {
        for (const IncludeInfo * header : headers) {
            dep_file_names.emplace_back(_process_makefile_target(header->file_path));
        }
    }
    

    return dep_file_names;

}   /* DepFilesState::_calculate_deps_for_headers() */


std::string DepFilesState::_create_depfile_name
    (
    const CompilerCall &    compiler_call
    ) const
{
 
    if (_mf_flag_path.empty() == false) {
        return _mf_flag_path;
    }
    
    if (compiler_call.get_output_obj_file().empty() == false) {
        return std::filesystem::path(compiler_call.get_output_obj_file())
            .replace_extension(".o.d")
            .string();
    }
    
    return std::filesystem::path(compiler_call.get_input_src_file())
        .filename()
        .replace_extension(".o.d")
        .string();

}   /* DepFilesState::_create_depfile_name() */


std::vector<const IncludeInfo *> DepFilesState::_ignore_system_headers
    (
    const CmdLineIncludeDirs &      system_include_dirs,
    const std::vector<const IncludeInfo *>
                                    filtered_headers
    ) const
{
    std::vector<const IncludeInfo *> processed_headers;
    bool is_system = false;
    
    for (const IncludeInfo * hdr_ptr : filtered_headers) {
        is_system = false;
        for (const std::string & sys_dir : system_include_dirs.dash_isystem) {
            if (hdr_ptr->file_path.starts_with(sys_dir)) {
                is_system = true;
                break;
            }
        }

        if (is_system == false) {
            processed_headers.push_back(hdr_ptr);
        }

    }

    return processed_headers;
}


std::string DepFilesState::_process_makefile_target
    (
    const std::string & target
    ) const 
{

        std::string processed_target;
        for (ssize_t tgt_idx = 0; tgt_idx < target.size(); ++tgt_idx) {
            switch (target[tgt_idx]) {
                case ' ':
                case '\t':
                    for (ssize_t aux_idx = tgt_idx - 1; 
                        aux_idx >= 0 && target[aux_idx] == '\\';
                        --aux_idx) {
                        processed_target += '\\';
                    }
                    
                    processed_target += '\\';

                    break;
                
                case '$':
                    processed_target += '$';
                    break;
                case '#':
                    processed_target += '\\';
                    break;
                default:
                    break;
            }
            
            processed_target += target[tgt_idx];
        }

        return processed_target;

}   /* DepFilesState::_process_makefile_target() */

