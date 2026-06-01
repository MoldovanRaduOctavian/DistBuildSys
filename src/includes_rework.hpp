#ifndef INCLUDES_REWORK_HPP
#define INCLUDES_REWORK_HPP

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/uuid/detail/sha1.hpp>

#include "yaml_cfg.hpp"

using sha1_type = boost::uuids::detail::sha1::digest_type;

struct CmdLineIncludeDirs {
    std::vector<std::string> dash_i;
    std::vector<std::string> dash_isystem;
    std::vector<std::string> dash_iquote;
    std::vector<std::string> dash_include;

    CmdLineIncludeDirs(bool do_system_includes = false)
    {
        if (do_system_includes) {
            find_system_includes(YamlConfig::instance().compiler_bin);
        }
    };
    
    bool find_system_includes
        (
        const std::string &     compiler
        );

    std::vector<std::string> cnvt_to_cmd_line_args() const {
        std::vector<std::string> cmd_line_args;
        for (const std::string & arg_i : dash_i) {
            cmd_line_args.emplace_back("-I");
            cmd_line_args.emplace_back(arg_i);
        }

        for (const std::string & arg_iquote : dash_iquote) {
            cmd_line_args.emplace_back("-iquote");
            cmd_line_args.emplace_back(arg_iquote);
        }

        for (const std::string & arg_isystem : dash_isystem) {
            cmd_line_args.emplace_back("-isystem");
            cmd_line_args.emplace_back(arg_isystem);
        }

        for (const std::string & args_include : dash_include) {
            cmd_line_args.emplace_back("-include");
            cmd_line_args.emplace_back(args_include);
        }

        return cmd_line_args;

    }
    
    void append_include_dirs
        (
        const CmdLineIncludeDirs & other_include_dirs
        )
    {
        dash_i.insert
            (
            dash_i.end(), 
            other_include_dirs.dash_i.begin(),
            other_include_dirs.dash_i.end()
            );
        dash_isystem.insert
            (
            dash_isystem.end(),
            other_include_dirs.dash_isystem.begin(),
            other_include_dirs.dash_isystem.end()
            );
        dash_iquote.insert
            (
            dash_iquote.end(),
            other_include_dirs.dash_iquote.begin(),
            other_include_dirs.dash_iquote.end()
            );
        dash_include.insert
            (
            dash_include.end(),
            other_include_dirs.dash_include.begin(),
            other_include_dirs.dash_include.end()
            );

    }

};


struct IncludeDirective {
    enum class IncludeType : uint8_t {
        INCLUDE_TYPE_ANGLE,
        INCLUDE_TYPE_QUOTE,
        INCLUDE_TYPE_COUNT
    };

    // This only contains the name of the header
    // or a partial path, not full aboslute path
    std::string inc_content; 
    IncludeType inc_type;

    IncludeDirective(
        const std::string &     _inc_content,
        IncludeType             _inc_type
    ) :
    inc_content(_inc_content),
    inc_type(_inc_type)
    {};
};


struct IncludeInfo {
    std::string     file_path;
    std::vector<IncludeDirective> 
                    child_includes;
    uint64_t        include_sz_bytes;
    // swap this for boost sha1 type?
    // or transform boost hash to this?
    std::string     include_hash;
    
    IncludeInfo() :
        file_path(""),
        child_includes(),
        include_sz_bytes(0),
        include_hash("")
    {};

    IncludeInfo(
        const std::string & _file_path,
        const std::vector<IncludeDirective> &
                            _child_includes,
        uint64_t            _include_sz_bytes,
        const std::string & _include_hash
                
    ) :
    file_path(_file_path),
    child_includes(_child_includes),
    include_sz_bytes(_include_sz_bytes),
    include_hash(_include_hash)
    {};
};

// This is persistent throughout the lifetime
// of the program
class IncludesCache {

private:

    // Mapping from include directive content 
    // to header absolute path

    // This has to be renamed, this is not descriptive
    // Complete absolute path of include and include
    // directive content?
    
    CmdLineIncludeDirs &    _system_include_dirs;

    /* #include ... -> absolute path */
    std::unordered_map<std::string, std::string>
                            _include_abs_paths;

    std::unordered_map<std::string, IncludeInfo>
                            _include_cache_data;
    
    // Store the paths that do not exist, to not check them again
    std::unordered_set<std::string>
                            _does_not_exist;

    mutable std::mutex      _mut;

public:

    IncludesCache
        (
        CmdLineIncludeDirs & system_include_dirs
        ) :
        _system_include_dirs(system_include_dirs)
        {};

    void add_include_entry
        (
        const std::string & include_name,
        const std::string & include_abs_path,
        const IncludeInfo & include_info
        )
    {
        std::lock_guard<std::mutex> lock(_mut);
        _include_abs_paths[include_name]    = include_abs_path;
        _include_cache_data[include_name]   = include_info;
    }
    
    const CmdLineIncludeDirs & get_sys_include_dirs() const {
        std::lock_guard<std::mutex> lock(_mut);
        return _system_include_dirs;
    }

    const std::string * get_include_abs_path
        (
        const std::string & include_name
        ) const
    {
        std::lock_guard<std::mutex> lock(_mut);
        auto inc_pair = _include_abs_paths.find(include_name);
        if (inc_pair == _include_abs_paths.end()) {
            return nullptr;
        }

        return &inc_pair->second;
    }
    
    const IncludeInfo * get_include_info
        (
        const std::string & include_name
        ) const
    {
        std::lock_guard<std::mutex> lock(_mut);
        auto inc_pair = _include_cache_data.find(include_name);
        if (inc_pair == _include_cache_data.end()) {
            return nullptr;
        }

        return &inc_pair->second;
    }
   
    void mark_src_nonexistent
        (
        const std::string & marked_abs_path
        )
    {
        std::lock_guard<std::mutex> lock(_mut);
        _does_not_exist.insert(marked_abs_path);       
    }

    bool is_src_nonexistent
        (
        const std::string & checked_abs_path
        ) const
    {
        std::lock_guard<std::mutex> lock(_mut);
        return _does_not_exist.contains(checked_abs_path);
    }

};

// For thread safety, one of this will be instantiated
// on every compiler invocation 
// we will have to deal with loads of these in parallel

class SourceFileParser {
       
    CmdLineIncludeDirs &        _system_include_dirs;
    IncludesCache &             _includes_cache;

    std::unordered_map<std::string, IncludeInfo>
                                _inc_directives;
     
public:
    
    SourceFileParser 
        (
        CmdLineIncludeDirs &    system_include_dirs,
        IncludesCache &         includes_cache 
        ) :
        _system_include_dirs(system_include_dirs),
        _includes_cache(includes_cache)
        {};
    
    bool parse_source_file_includes
        (
        const std::string & cpp_abs_path,       /* absolute path of parsed source file      */
        const std::vector<std::string> &
                            cmd_line_includes   /* includes specified in the cmd line?!?!   */
        );
    
    std::unordered_map<std::string, IncludeInfo> &
        get_include_directives()
    {
        return _inc_directives;
    }

private:
    
    bool _process_include_directive
        (
        const std::string &         parent_src_abs_path,
        const IncludeDirective &    include_directive /* #include ... to be processed */
        );
    
    bool _try_resolve_header
        (
        const std::string &         header_abs_path,    /* potential header abs path to check */
        const IncludeDirective &    header_inc_directive
                                                        /* resolved header include directive  */
        );

};


#endif  /* INCLUDES_REWORK_HPP */

