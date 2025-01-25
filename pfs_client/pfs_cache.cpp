#include "pfs_api.hpp"
#include "pfs_cache.hpp"
#include "pfs_proto/pfs_metaserver.pb.h"
#include "pfs_proto/pfs_metaserver.grpc.pb.h"
#include "pfs_proto/pfs_fileserver.pb.h"
#include "pfs_proto/pfs_fileserver.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <unordered_map>
#include <list>


void cache_func_temp() {
    printf("%s: called.\n", __func__);
}

ClientCache::ClientCache(size_t size, ClientState& state) 
    : cache_size(size), client_state(state) {}

void ClientCache::initialize() {
    lru_list.clear();
    cache_map.clear();
}

bool ClientCache::hasBlock(const std::string& key) {
    return cache_map.find(key) != cache_map.end();
}

void ClientCache::addBlock(const std::string& key, const std::string& data, int token) {
    if (cache_map.size() >= cache_size) {
        evictBlock();
    }
    lru_list.push_front(key);
    cache_map[key] = {data, false, token};
}

void ClientCache::evictBlock() {
    if (!lru_list.empty()) {
        std::string key = lru_list.back();
        lru_list.pop_back();
        cache_map.erase(key);
        client_state.num_evictions++; // Increment eviction stat
    }
}

void ClientCache::invalidateBlock(const std::string& key) {
    if (cache_map.find(key) != cache_map.end()) {
        lru_list.remove(key);
        cache_map.erase(key);
        client_state.num_invalidations++; // Increment invalidation stat
    }
}