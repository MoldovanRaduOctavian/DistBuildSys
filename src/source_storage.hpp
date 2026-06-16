#ifndef SOURCE_STORAGE_HPP
#define SOURCE_STORAGE_HPP

#include <string>

#include "lru_storage.hpp"

class SourceStorage {

public:
    struct SourceRecord {
        size_t      size;
        std::string abs_path;
    };

private:
    LruStorage<SourceRecord> _src_store;

};

#endif  /* SOURCE_STORAGE_HPP */

