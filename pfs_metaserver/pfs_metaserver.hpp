#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <cstring>
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
#include <shared_mutex>


#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>

#include "pfs_common/pfs_common.hpp"

#include "pfs_proto/pfs_metaserver.pb.h"
#include "pfs_proto/pfs_metaserver.grpc.pb.h"


extern std::unique_ptr<pfsmeta::MetadataServer::Stub> metadata_stub;

int metaserverUp(const std::string& metadata_server_address);

int pfs_initialize();
bool metaserverPing();
int pfs_create(const char* filename, int stripe_width);
int pfs_open(const char *filename, int mode);
int pfs_fstat(int fd, struct pfs_metadata *meta_data);


struct FileDescriptor {
    std::string filename; // The name of the file
    int mode;             // Open mode: 1 for read, 2 for write
    int64_t offset;       // Current file offset (optional)
    // Default constructor
    FileDescriptor() : filename(""), mode(0), offset(0) {}

    // Constructor for initialization
    FileDescriptor(const std::string& file, int open_mode, int64_t file_offset = 0)
        : filename(file), mode(open_mode), offset(file_offset) {}
};

// Token structure to manage client tokens
struct Token {
    int client_id;          // Client ID associated with the token
    int fd;                 // File descriptor associated with the token
    std::string filename;   // File name for the token
    int token_type;         // 1 for READ, 2 for WRITE
    int64_t start_byte;     // Start byte range of the token
    int64_t end_byte;       // End byte range of the token

    Token(int c_id, int file_d, const std::string& file, int type, int64_t start, int64_t end)
        : client_id(c_id), fd(file_d), filename(file), token_type(type), start_byte(start), end_byte(end) {}
};


struct ClientState {
    int client_id;  // Unique client ID assigned by Metadata Server
    std::unordered_map<int,  FileDescriptor> open_files;  // Open files
    std::vector<Token> tokens;  // Active tokens
    std::mutex state_mutex;  // Thread-safety
    int num_read_hits = 0;
    int num_write_hits = 0;
    int num_evictions = 0;
    int num_writebacks = 0;
    int num_invalidations = 0;
    int num_close_writebacks = 0;
    int num_close_evictions = 0;

    ClientState() : client_id(-1) {}
};




std::unordered_map<std::string, std::vector<Token>> token_table;

std::shared_mutex token_table_mutex;


std::vector<Token> request_tokens;

std::vector<Token> revoke_tokens;

std::mutex revoke_mutex;

std::shared_mutex metadata_mutex; 
std::unordered_map<std::string, pfsmeta::FileMetadata> file_metadata_map; 





// Structure to hold metadata (if needed in the future)
// struct pfs_metadata {
//     char filename[256]; // File name
//     uint64_t file_size; // File size
//     time_t ctime;       // Creation time
//     time_t mtime;       // Modification time
//     struct pfs_filerecipe recipe; // File recipe
// };
