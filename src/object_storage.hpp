#ifndef OBJECT_STORAGE_HPP
#define OBJECT_STORAGE_HPP

#include "lru_storage.hpp"

class ObjectStorage {

public:

    struct ObjectNode {
        size_t size;
    };   
     
    /* 2gb of object cache to begin with */
    static constexpr size_t OBJ_STORE_CAPACITY_BYTES = 2048ull * 1024 * 1024;
    static constexpr const char * OBJ_STORE_PATH = "/tmp/distbuild/obj_store";

private:
    
    LruStorage<ObjectNode> _obj_files;

public:

    ObjectStorage() :
        _obj_files(OBJ_STORE_CAPACITY_BYTES)
    {};

};

#endif  /* OBJECT_STORAGE_HPP */
