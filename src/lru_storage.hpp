#ifndef LRU_STORAGE_HPP
#define LRU_STORAGE_HPP

#include <list>
#include <mutex>
#include <string>
#include <unordered_map>


template<typename T>
class LruStorage {

private:
    
    using list_iter         = typename std::list<T>::iterator;

    std::list<std::pair<std::string, T>>            
                            _storage_nodes;
    std::unordered_map<std::string, list_iter>
                            _node_iterators;
    
    size_t                  _store_usage;
    size_t                  _store_capacity;
    
    mutable std::mutex      _mut;

public:

    LruStorage(const size_t store_capacity) :
        _storage_nodes(),
        _node_iterators(),
        _store_usage(0),
        _store_capacity(store_capacity)
    {};

    void clear_storage() {
        std::lock_guard<std::mutex> lock(_mut);
        _node_iterators.clear();
        _storage_nodes.clear();
        _store_usage = 0;
    }

    const T * get_value(const std::string & key) {
        std::lock_guard<std::mutex> lock(_mut);
        auto searched_val_iter = _node_iterators.find(key);
        if (searched_val_iter == _node_iterators.end()) {
            return nullptr;
        }
       
        // Move item to the beginning of the list
        _storage_nodes.splice(_storage_nodes.begin(), _storage_nodes, searched_val_iter->second);        

        const T * searched_val_ptr = &(searched_val_iter->second);
        return searched_val_ptr;
    }
    
    bool store_value
        (
        const std::string & key, 
        const T &           value
        )
    {
        std::lock_guard<std::mutex> lock(_mut);
        if (_node_iterators.find(key) != _node_iterators.end()) {
            return false;
        }
        
        while (_store_usage + value.size > _store_capacity) {
            auto storage_last = _storage_nodes.back();
            const T & last_value = storage_last.second;
            _store_usage -= last_value.size;
            _node_iterators.erase(storage_last.first);
            _storage_nodes.pop_back();
        }

        _storage_nodes.emplace_front(key, value);
        _node_iterators.insert({key, _storage_nodes.begin()});
        _store_usage += value.size; 
        return true;
    }

};

#endif  /* LRU_STORAGE_HPP */

