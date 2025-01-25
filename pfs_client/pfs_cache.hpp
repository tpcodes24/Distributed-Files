#pragma once

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstdbool>
#include <vector>
#include <list>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
//#include <semaphore>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <set>


#include "pfs_common/pfs_config.hpp"
#include "pfs_common/pfs_common.hpp"
#include "pfs_api.hpp"

void cache_func_temp();

// Represents a single cache block
struct CacheBlock {
    std::string data; // Data for the block
    bool dirty;       // Indicates if the block has been modified
    int token;        // Token for read/write permissions
};

// Cache class
class ClientCache {
private:
    size_t cache_size;                     // Maximum number of blocks
    ClientState& client_state;             // Reference to the client state
    std::list<std::string> lru_list;       // LRU list for cache management
    std::unordered_map<std::string, CacheBlock> cache_map; // Key: filename+block_id -> CacheBlock

public:
    ClientCache(size_t size, ClientState& state);

    void initialize();

    bool hasBlock(const std::string& key);

    void addBlock(const std::string& key, const std::string& data, int token);

    void evictBlock();

    void invalidateBlock(const std::string& key);
};