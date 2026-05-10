#ifndef DEP_FILES_HPP
#define DEP_FILES_HPP

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "compiler_call.hpp"
#include "includes_rework.hpp"

struct DepTarget {
    std::string     target;
    std::vector<std::string>
                    dep_files;

    DepTarget
        (
        const std::string & _target,
        const std::vector<std::string> & 
                            _dep_files 
        ) :
        target(_target),
        dep_files(_dep_files)
        {};
    
};

inline std::string replace_all
    (
    std::string     str,
    const std::string & 
                    from,
    const std::string & 
                    to
    )
{
    size_t pos = 0;

    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }

    return str;

}   /* replace_all() */


inline std::string escape_gnu_make_spaces
    (
    const std::string & dep_item
    )
{
    std::string dep_item_ret = replace_all(dep_item, "\n", "\\\n");
    dep_item_ret = replace_all(dep_item_ret, " ", "\\ ");
    dep_item_ret = replace_all(dep_item_ret, ":", "\\:");
    return dep_item_ret;
    
}   /* escape_gnu_make_spaces() */


inline size_t parse_skip_spaces
    (
    const std::string & 
            c,
    size_t  start_offset
    )
{
    size_t offset = start_offset;

    while (offset < c.size() &&
           std::isspace(
               static_cast<unsigned char>(c[offset])))
    {
        ++offset;
    }

    return offset;

}   /* parse_skip_spaces() */


struct ParseResultTargetName {
    std::string target_name;
    size_t      offset;

};

inline ParseResultTargetName parse_target_name
    (
    const std::string & 
            c,
    size_t  start_offset
    )
{
    size_t offset = start_offset;
    std::string target_name;

    while (offset < c.size()) {
        if (c[offset] == ':') {
            ++offset;

            return {
                escape_gnu_make_spaces(target_name),
                offset
            };
        }
        else if (c[offset] == '\n') {
            break;
        }
        else if (c[offset] == '\\') {
            if (offset + 1 < c.size() &&
                c[offset + 1] != '\n')
            {
                target_name += c[offset + 1];
            }

            offset += 2;
        }
        else if (c[offset] == ' ') {
            if (target_name.empty() ||
                target_name.back() != ' ')
            {
                target_name += ' ';
            }

            ++offset;
        }
        else {
            target_name += c[offset];
            ++offset;
        }
    }

    throw std::runtime_error(
        "':' expected after target name");

}   /* parse_target_name() */


struct ParseResultDepItem {
    std::string dep_item_name;
    size_t      offset;
};

inline ParseResultDepItem parse_next_dep_item
    (
    const std::string & 
            c,
    size_t  start_offset
    )
{
    size_t offset = start_offset;

    while (offset < c.size()) {
        if (c[offset] == ' ') {
            ++offset;
        }
        else if (c[offset] == '\\') {
            offset += 2;
        }
        else {
            break;
        }
    }

    if (offset >= c.size()) {
        return {"", offset};
    }

    if (c[offset] == '\n') {
        return {"", offset + 1};
    }

    std::string dep_item_name;

    while (offset < c.size()) {
        if (c[offset] == ' ' ||
            c[offset] == '\n')
        {
            break;
        }
        else if (c[offset] == '\\') {
            if (offset + 1 < c.size()) {
                dep_item_name += c[offset + 1];
            }

            offset += 2;
        }
        else {
            dep_item_name += c[offset];
            ++offset;
        }
    }

    return {dep_item_name, offset};

}   /* parse_next_dep_item() */


struct DepFile {
    std::vector<DepTarget> dep_targets;
    
    std::vector<std::string> find_dep_list_by_target_name
        (
        const std::string & target_name
        ) const
    {
        for (const auto& d_target : dep_targets) {
            if (d_target.target == target_name) {
                return d_target.dep_files;
            }
        }

        return {};
    }

    std::string write_to_bytes() const
    {
        std::ostringstream out;

        for (size_t i = 0; i < dep_targets.size(); ++i) {
            const auto& d_target = dep_targets[i];

            out << d_target.target << ":";

            if (!d_target.dep_files.empty()) {
                out << " "
                    << escape_gnu_make_spaces(
                           d_target.dep_files[0]);

                for (size_t j = 1;
                     j < d_target.dep_files.size();
                     ++j)
                {
                    out << " \\\n  "
                        << escape_gnu_make_spaces(
                               d_target.dep_files[j]);
                }
            }

            out << "\n";

            if (i + 1 < dep_targets.size()) {
                out << "\n";
            }
        }

        return out.str();

    }

    void write_to_file(
        const std::filesystem::path& dep_file_name) const
    {
        std::ofstream file(dep_file_name, std::ios::binary);

        if (!file) {
            throw std::runtime_error(
                "failed to open output file: " +
                dep_file_name.string());
        }

        file << write_to_bytes();
    }

    void parse_dep_file_contents
        (
        const std::string & c
        )
    {
        size_t offset = 0;

        while (true) {
            offset = parse_skip_spaces(c, offset);

            if (offset >= c.size()) {
                break;
            }

            auto target_result =
                parse_target_name(c, offset);

            std::string target_name =
                target_result.target_name;

            offset = target_result.offset;

            std::vector<std::string> dep_items;

            while (true) {
                auto dep_result =
                    parse_next_dep_item(c, offset);

                offset = dep_result.offset;

                if (dep_result.dep_item_name.empty()) {
                    break;
                }

                dep_items.push_back(
                    dep_result.dep_item_name);
            }

            dep_targets.push_back({
                target_name,
                dep_items
            });
        }
    }

};


inline DepFile make_dep_file_from_bytes
    (
    const std::string & d_file_contents
    )
{
    DepFile result;
    result.parse_dep_file_contents(d_file_contents);
    return result;

}   /* make_dep_file_from_bytes() */


inline DepFile make_dep_file_from_file
    (
    const std::filesystem::path & d_file_name
    )
{
    std::ifstream file(d_file_name, std::ios::binary);

    if (!file) {
        throw std::runtime_error(
            "failed to open file: " + d_file_name.string());
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    return make_dep_file_from_bytes(buffer.str());

}   /* make_dep_file_from_file() */


class DepFilesState {

    bool        _md_flag_present;
    bool        _mmd_flag_present;
    bool        _mp_flag_present;
    std::string _mf_flag_path;
    std::string _mt_flag_paths;

public:
    
    void set_md_flag_present(bool md_flag_present) {
        _md_flag_present = true;
    }
    
    void set_mmd_flag_present(bool mmd_flag_present) {
        _mmd_flag_present = true;
    }
    
    void set_mp_flag_present(bool mp_flag_present) {
        _mp_flag_present = mp_flag_present;
    }
    
    void add_mf_flag_path(const std::string & mf_flag_path) {
        _mf_flag_path += mf_flag_path;

    }
     
    void add_mt_flag_path(const std::string & mt_flag_path) {
        if (_mt_flag_paths.empty() == false) {
            _mt_flag_paths += " \\\n";
        }
        _mt_flag_paths += mt_flag_path;

    }

    void add_mq_flag_path(const std::string & mq_flag_path) {
        if (_mt_flag_paths.empty() == false) {
            _mt_flag_paths += " \\\n";
        }
        _mt_flag_paths += _process_makefile_target(mq_flag_path);

    }

    
    bool get_md_flag_present() const {
        return _md_flag_present;
    }

    bool get_mmd_flag_present() const {
        return _mmd_flag_present;
    }
    
    bool get_mp_flag_present() const {
        return _mp_flag_present;
    }
   
    const std::string & get_mt_flag_paths() const {
        return _mt_flag_paths;
    }

    const std::string & get_mf_flag_paths() const {
        return _mf_flag_path;
    }

    bool is_depfile_needed() const {
        return _mf_flag_path.empty() == false || _md_flag_present == true;
    }
    
    std::string generate_dep_file
        (
        const CompilerCall &    compiler_call,
        const std::vector<const IncludeInfo *>
                                headers
        ) const;

private:
        
    const std::vector<std::string> _calculate_deps_for_headers
        (
        const CompilerCall &    compiler_call,
        const std::vector<const IncludeInfo *>
                                headers
        ) const;

    std::string _create_depfile_name
        (
        const CompilerCall &    compiler_call
        ) const;

    std::vector<const IncludeInfo *> _ignore_system_headers
        (
        const CmdLineIncludeDirs &      system_include_dirs,
        const std::vector<const IncludeInfo *>
                                        filtered_headers
        ) const;

    std::string _process_makefile_target
        (
        const std::string & target
        ) const; 

};

#endif /* DEP_FILES_HPP */

