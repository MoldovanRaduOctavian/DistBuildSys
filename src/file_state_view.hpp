#ifndef FILE_STATE_VIEW_HPP
#define FILE_STATE_VIEW_HPP

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <boost/uuid/detail/sha1.hpp>


struct FileStateView {
    
    enum class Status : uint8_t {
        FILE_STATUS_INITIAL,
        FILE_STATUS_UPLOADING,
        FILE_STATUS_AVAILABLE,
        FILE_STATUS_FAULT,
        FILE_STATUS_COUNT
    };
    
    FileStateView() {};
    FileStateView
        (
        uint64_t _file_sz_bytes,
        const std::string & _file_sha1,
        const std::string & _server_file_path
        ) :
        file_sz_bytes(_file_sz_bytes),
        file_sha1(_file_sha1),
        file_status(Status::FILE_STATUS_INITIAL),
        up_start_time(0),
        server_file_path(_server_file_path)
        {};

    uint64_t    file_sz_bytes;
    std::string file_sha1;
    Status      file_status; 
    uint64_t    up_start_time;
    std::string server_file_path;

};

struct FileTransferState {
    bool            is_finished;
    std::ofstream   file_out_stream;
    std::string     session_id;
    std::string     filename;
    uint64_t        last_seq_no;
    uint64_t        last_offset;

    FileTransferState
        (
        const std::string & _session_id,
        const std::string & _filename
        ) :
        is_finished(false),
        file_out_stream(_filename, std::ios::binary),
        session_id(_session_id),
        filename(_filename),
        last_seq_no(0),
        last_offset(0)
    {};

};

inline std::string cnvt_client_to_server_path
    (
    const std::string & current_working_dir,
    const std::string & client_path
    )
{
    auto is_system_path = [](const std::string & path) -> bool {
        return path.starts_with("/usr/src")
            || path.starts_with("/usr/local")
            || path.starts_with("/Library");
    };
    
    // Pay a lot of attention to this
    return is_system_path(client_path)
        ? client_path
        : std::filesystem::path(
            std::filesystem::path(current_working_dir)
            / std::filesystem::path(client_path).relative_path()
        ).string();

}   /* cnvt_client_to_server_path() */


inline std::string cnvt_server_to_client_path
    (
    const std::string & current_working_dir,
    const std::string & server_path
    ) 
{

    return server_path.starts_with(current_working_dir)
        ? server_path.substr(current_working_dir.size()) 
        : server_path;   

}   /* cnvt_server_path_to_client_path() */


inline bool generate_file_sha1
    (
    std::ifstream & input_stream,
    boost::uuids::detail::sha1::digest_type &     
                    file_hash
    )
{
    if (!input_stream.is_open()) {
        return false;
    }
    
    boost::uuids::detail::sha1  sha1;
    std::vector<uint8_t>        stream_buff;
    std::for_each(std::istreambuf_iterator<char>(input_stream),
                  std::istreambuf_iterator<char>(),
                  [&stream_buff](const char c) {
                    stream_buff.emplace_back(c);
                  });
    
    sha1.process_bytes(stream_buff.data(), stream_buff.size());
    sha1.get_digest(file_hash);

    input_stream.clear();
    input_stream.seekg(0, std::ios::beg);
    return true;

}   /* generate_file_sha1() */


inline std::string generate_obj_file_path
    (
    const std::string & out_obj_dir,
    const std::string & client_id,
    const std::string & session_id
    )
{
    return out_obj_dir + "/" + client_id + "_" + session_id + ".o";

}   /* generate_obj_file_path() */

#endif /* FILE_STATE_VIEW_HPP */

