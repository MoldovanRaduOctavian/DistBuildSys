#include "includes_rework.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <boost/process.hpp>
#include <boost/regex.hpp>
#include <boost/uuid/detail/sha1.hpp>

#include "file_state_view.hpp"

#if 0

static bool find_file_includes
    (
    std::ifstream &     input_stream,
    std::vector<IncludeDirective> & 
                        found_includes
    )
{
    if (!input_stream.is_open()) {
        return false;
    }

    found_includes.clear();

    std::string line;

    while (std::getline(input_stream, line)) {

        // --- find #include ---
        std::string::size_type pos = line.find("#include");
        if (pos == std::string::npos) {
            continue;
        }

        pos += 8; // length of "#include"

        // --- skip whitespace ---
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
            ++pos;
        }

        if (pos >= line.size()) {
            continue;
        }

        IncludeDirective::IncludeType type;
        std::string included_file;

        // --- angle includes <...> ---
        if (line[pos] == '<') {
            type = IncludeDirective::IncludeType::INCLUDE_TYPE_ANGLE;
            ++pos;

            auto end = line.find('>', pos);
            if (end == std::string::npos) {
                continue;
            }

            included_file = line.substr(pos, end - pos);
        }

        // --- quote includes "..." ---
        else if (line[pos] == '"') {
            type = IncludeDirective::IncludeType::INCLUDE_TYPE_QUOTE;
            ++pos;

            auto end = line.find('"', pos);
            if (end == std::string::npos) {
                continue;
            }

            included_file = line.substr(pos, end - pos);
        }
        else {
            continue;
        }

        found_includes.emplace_back(included_file, type);
    }

    input_stream.clear();
    input_stream.seekg(0, std::ios::beg);
    return true;
}

#endif

#if 0

static bool find_file_includes
(
    std::ifstream& input_stream,
    std::vector<IncludeDirective>& found_includes
)
{
    if (!input_stream.is_open())
    {
        return false;
    }

    found_includes.clear();

    std::string line;

    while (std::getline(input_stream, line))
    {
        // Find the preprocessor directive marker.
        std::string::size_type pos = line.find('#');
        if (pos == std::string::npos)
        {
            continue;
        }

        ++pos; // Skip '#'

        // Allow whitespace between '#' and 'include'.
        while (pos < line.size() &&
               std::isspace(static_cast<unsigned char>(line[pos])))
        {
            ++pos;
        }

        constexpr char include_keyword[] = "include";
        constexpr std::size_t include_length = sizeof(include_keyword) - 1;

        if (line.compare(pos, include_length, include_keyword) != 0)
        {
            continue;
        }

        pos += include_length;

        // Allow whitespace between 'include' and the filename.
        while (pos < line.size() &&
               std::isspace(static_cast<unsigned char>(line[pos])))
        {
            ++pos;
        }

        if (pos >= line.size())
        {
            continue;
        }

        IncludeDirective::IncludeType type;
        std::string included_file;

        if (line[pos] == '<')
        {
            type = IncludeDirective::IncludeType::INCLUDE_TYPE_ANGLE;
            ++pos;

            auto end = line.find('>', pos);
            if (end == std::string::npos)
            {
                continue;
            }

            included_file = line.substr(pos, end - pos);
        }
        else if (line[pos] == '"')
        {
            type = IncludeDirective::IncludeType::INCLUDE_TYPE_QUOTE;
            ++pos;

            auto end = line.find('"', pos);
            if (end == std::string::npos)
            {
                continue;
            }

            included_file = line.substr(pos, end - pos);
        }
        else
        {
            continue;
        }

        found_includes.emplace_back(std::move(included_file), type);
    }

    // Reset the stream for future use.
    input_stream.clear();
    input_stream.seekg(0, std::ios::beg);

    return true;
}

#endif

static bool find_file_includes
(
    std::ifstream& input_stream,
    std::vector<IncludeDirective>& found_includes
)
{
    if (!input_stream.is_open())
    {
        return false;
    }

    found_includes.clear();

    std::string line;

    while (std::getline(input_stream, line))
    {
        auto pos = line.find('#');
        if (pos == std::string::npos)
        {
            continue;
        }

        ++pos;

        // skip whitespace after '#'
        while (pos < line.size() &&
               std::isspace(static_cast<unsigned char>(line[pos])))
        {
            ++pos;
        }

        constexpr char include_kw[] = "include";
        constexpr char include_next_kw[] = "include_next";

        bool matched = false;

        // Accept include_next first (longer token)
        if (line.compare(pos, sizeof(include_next_kw) - 1, include_next_kw) == 0)
        {
            pos += sizeof(include_next_kw) - 1;
            matched = true;
        }
        else if (line.compare(pos, sizeof(include_kw) - 1, include_kw) == 0)
        {
            pos += sizeof(include_kw) - 1;
            matched = true;
        }

        if (!matched)
        {
            continue;
        }

        // skip whitespace after keyword
        while (pos < line.size() &&
               std::isspace(static_cast<unsigned char>(line[pos])))
        {
            ++pos;
        }

        if (pos >= line.size())
        {
            continue;
        }

        IncludeDirective::IncludeType type;
        std::string included_file;

        if (line[pos] == '<')
        {
            type = IncludeDirective::IncludeType::INCLUDE_TYPE_ANGLE;
            ++pos;

            auto end = line.find('>', pos);
            if (end == std::string::npos)
            {
                continue;
            }

            included_file = line.substr(pos, end - pos);
        }
        else if (line[pos] == '"')
        {
            type = IncludeDirective::IncludeType::INCLUDE_TYPE_QUOTE;
            ++pos;

            auto end = line.find('"', pos);
            if (end == std::string::npos)
            {
                continue;
            }

            included_file = line.substr(pos, end - pos);
        }
        else
        {
            continue;
        }

        found_includes.emplace_back(std::move(included_file), type);
    }

    input_stream.clear();
    input_stream.seekg(0, std::ios::beg);

    return true;
}

static bool requires_inc_storage
    (
    const std::string &     include_filename,
    const CmdLineIncludeDirs &
                            system_inc_dirs
    )
{
    bool is_dash_i = std::any_of(
                system_inc_dirs.dash_i.begin(), 
                system_inc_dirs.dash_i.end(), 
                [&include_filename](const std::string & dash_i_include){
                    return include_filename.starts_with(dash_i_include);
                });

    if (is_dash_i) {
        return false;
    }
    
    bool is_dash_isystem = std::any_of(
                system_inc_dirs.dash_isystem.begin(),
                system_inc_dirs.dash_isystem.end(),
                [&include_filename](const std::string & dash_isystem_include) {
                    return include_filename.starts_with(dash_isystem_include);
                });

    return is_dash_isystem;

}


bool CmdLineIncludeDirs::find_system_includes
    (
    const std::string &     compiler
    )
{
    boost::process::ipstream    cpp_stream;
    
    // Process search path will break
    boost::process::child       cpp_proc(
        compiler,
        "-Wp,-v", "-x", 
        "c++", "/dev/null", "-fsyntax-only",
        boost::process::std_err > cpp_stream
    );
    
    boost::regex    inc_dir_regex(R"(^\s*(\/\S+))");
    boost::smatch   match;
    std::string     input_line;

    while (cpp_proc.running() 
            && std::getline(cpp_stream, input_line)
            && !input_line.empty()) {
        
        std::cout << input_line << '\n';
        if (input_line.find("ignoring") != std::string::npos) {
            continue;
        }

        if (boost::regex_match(input_line, match, inc_dir_regex)) {
            std::string include_path = match[1];
            if (include_path.starts_with("/usr/")) {
                std::filesystem::path abs_inc_path = 
                    std::filesystem::weakly_canonical(include_path);
                dash_isystem.emplace_back(abs_inc_path);
            }
            else {
                dash_i.emplace_back(include_path);
            }
        }

    }

    cpp_proc.wait();
    return true;
}


bool SourceFileParser::parse_source_file_includes
    (
    const std::string & cpp_abs_path,       /* absolute path of parsed source file      */
    const std::vector<std::string> &
                        cmd_line_includes   /* includes specified in the cmd line?!?!   */
    )
{

    std::cout << "BEFORE parse_source_file_includes begins\n";
    std::ifstream cpp_source_stream(cpp_abs_path);
    if (!cpp_source_stream.is_open()) {
        return false;
    }
    std::cout << "AFTER parse_source_file_includes_ began\n";

    // These are all treated as IncludeDirective angle (#include <...>)
    for (const std::string & cmd_line_inc_directive : cmd_line_includes) {
        if (!_process_include_directive(
                cpp_abs_path, 
                IncludeDirective(cmd_line_inc_directive, IncludeDirective::IncludeType::INCLUDE_TYPE_ANGLE)
            )) 
        {
            std::cout << "COULD NOT RESOLVE INCLUDE: " 
                      << cpp_abs_path << " - " << cmd_line_inc_directive
                      << '\n'; 
        }
    }

    std::vector<IncludeDirective> cpp_src_include_directives;
    if (!find_file_includes(cpp_source_stream, cpp_src_include_directives)) {
        return false;
    }
    
    for (const IncludeDirective & inc_directive : cpp_src_include_directives) {
        // Call processing function for each IncludeDirective
        // Process those recursively
        if (!_process_include_directive(cpp_abs_path, inc_directive)) {
            std::cout << "COULD NOT RESOLVE INCLUDE: " 
                      << cpp_abs_path << " - " << inc_directive.inc_content
                      << '\n';
                
        }
    }
    
    return true;

}   /* SourceFileParser::parse_source_file_includes() */


bool SourceFileParser::_process_include_directive
    (
    const std::string &         parent_src_abs_path,    /* path of cpp which has the inc */
    const IncludeDirective &    include_directive       /* #include ... to be processed  */
    )
{
    // Figure out all possible absolute paths for the processed include
    // Each kind of include gets processed differently
    // Search the include cache to see if it was processed before
    // Add it to the include cache if it was not processed at all
    // If the include file does not exist, then add that include to 
    // a list in the cache, to not have to process it again
    // Should this be called recursively?
    
    // Try to resolve the path of the current include directive
    // Compute size in bytes and SHA1 for the file
    
    // For each resolved possible header path:
    // 1) Look if it was processed before (exit if it is the case, it either has info or does not exist)
    // 2) Search for the file inforamtion in the includes cache
    // 3) If file does not exist at all, return false and mark it
    // 4) Add the file to cache
    // 5) Call this function recursively on the children #include s
    
    
    // Try to resolve the the path of the include
    // Is there a unique path for each include?
    if (include_directive.inc_type == IncludeDirective::IncludeType::INCLUDE_TYPE_ANGLE) {
        // Search for the resolved path of the include in the includes cache
        const std::string * inc_abs_path_ptr = 
            _includes_cache.get_include_abs_path(include_directive.inc_content);
        if (inc_abs_path_ptr != nullptr) {
            const IncludeInfo * cached_include_info = 
                        _includes_cache.get_include_info(*inc_abs_path_ptr);
            if (cached_include_info != nullptr) {
                _inc_directives[*inc_abs_path_ptr] = *cached_include_info;
                return true;
            }
        }
    }

    if (include_directive.inc_content.starts_with('/')) {
        // If it starts with '/', it means that the include is an absolute path
        // Maybe a function named try_resolve_header ???
        if (_try_resolve_header(include_directive.inc_content, include_directive)) {
            return true;
        }
    }

    if (include_directive.inc_type == IncludeDirective::IncludeType::INCLUDE_TYPE_QUOTE) {
        // See if parent_cpp_abs_path.directory + include_directive is a valid file
        // See if a directory from -Iquote ... + include directive is a valid file
        std::string header_absolute_path =
            std::filesystem::path(
                std::filesystem::path(parent_src_abs_path).parent_path() / include_directive.inc_content
            ).string();
        if (_try_resolve_header(header_absolute_path, include_directive)) {
            return true;
        }

        for (const std::string & dash_iquote_dir : _system_include_dirs.dash_iquote) {
            // See if dash_iquote_dir + include_directive is a valid file
            header_absolute_path =
                std::filesystem::path(
                    std::filesystem::path(dash_iquote_dir) / include_directive.inc_content
                ).string();
            if (_try_resolve_header(header_absolute_path, include_directive)) {
                return true;
            }
        }

    }

    for (const std::string & dash_i_dir : _system_include_dirs.dash_i) {
        // See if dash_i_dir + include_directive is a valid file
        std::string header_absolute_path =
            std::filesystem::path(
                std::filesystem::path(dash_i_dir) / include_directive.inc_content
            ).string();
        if (_try_resolve_header(header_absolute_path, include_directive)) {
            return true;
        }
    }    

    for (const std::string & dash_isystem_dir : _system_include_dirs.dash_isystem) {
        // See if dash_isystem + include_directive is a valid file
        std::string header_absolute_path =
            std::filesystem::path(
                std::filesystem::path(dash_isystem_dir) / include_directive.inc_content
            ).string();
        if (_try_resolve_header(header_absolute_path, include_directive)) {
            return true;
        }

    }
    
    return false;

}   /* SourceFileParser::_process_include_directive() */


bool SourceFileParser::_try_resolve_header
    (
    const std::string &         header_abs_path,    /* potential header abs path to check */
    const IncludeDirective &    header_inc_directive
                                                    /* resolved header include directive  */
    )
{
    if (_includes_cache.is_src_nonexistent(header_abs_path)) {
        return false;
    }

    if (_inc_directives.contains(header_abs_path)) {
        return true;
    }

    const IncludeInfo * include_info_ptr = 
        _includes_cache.get_include_info(header_inc_directive.inc_content);
    if (include_info_ptr == nullptr) {
        
        std::ifstream header_file_stream(header_abs_path);
        if (!header_file_stream.is_open()) {
            _includes_cache.mark_src_nonexistent(header_abs_path);
            return false;
        }

        sha1_type header_file_sha1 = {0};
        generate_file_sha1(header_file_stream, header_file_sha1);
        uint64_t header_file_sz_bytes = std::filesystem::file_size(header_abs_path);

        std::vector<IncludeDirective> child_include_directives;
        find_file_includes(header_file_stream, child_include_directives);

        auto header_include_info = IncludeInfo(
            header_abs_path,
            child_include_directives,
            header_file_sz_bytes,
            std::string(
                reinterpret_cast<const char *>(header_file_sha1),
                sizeof(header_file_sha1)
            )
        );
        
        bool should_cache = requires_inc_storage(header_abs_path, _system_include_dirs);
        if (should_cache) {
            _includes_cache.add_include_entry(
                header_inc_directive.inc_content, 
                header_abs_path, 
                header_include_info);
        }
         
        _inc_directives[header_abs_path] = std::move(header_include_info);        

        for(const IncludeDirective & child_inc_directive : child_include_directives) {
            _process_include_directive(header_abs_path, child_inc_directive);
        }    

    }
    else {
        _inc_directives[header_abs_path] = *include_info_ptr;
    }            

    return true;

}   /* SourceFileParser::_try_resolve_header() */


