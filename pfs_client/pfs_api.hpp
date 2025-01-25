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
#include <shared_mutex>

#include "pfs_common/pfs_config.hpp"
#include "pfs_common/pfs_common.hpp"

struct pfs_filerecipe {
    int stripe_width;

    // Additional...

};

struct pfs_metadata {
    // Given metadata
    char filename[256];
    uint64_t file_size;
    time_t ctime;
    time_t mtime;
    struct pfs_filerecipe recipe;

    // Additional...

};

struct pfs_execstat {
    long num_read_hits;
    long num_write_hits;
    long num_evictions;
    long num_writebacks;
    long num_invalidations;
    long num_close_writebacks;
    long num_close_evictions;
};

// File descriptor information
// struct FileDescriptor {
//     std::string filename;
//     int mode;  // 1 for read, 2 for read/write
// };
struct FileDescriptor {
    std::string filename; // The name of the file
    int mode;             // Open mode: 1 for read, 2 for write
    int64_t offset;       // Current file offset
    size_t filesize;      // File size (added field)

    // Default constructor
    FileDescriptor() : filename(""), mode(0), offset(0), filesize(0) {}

    // Constructor for initialization
    FileDescriptor(const std::string& file, int open_mode, size_t file_size, int64_t file_offset = 0)
        : filename(file), mode(open_mode), offset(file_offset), filesize(file_size) {}
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



// Client State
struct ClientState {
    int client_id;  // Unique client ID assigned by Metadata Server
    std::unordered_map<int, FileDescriptor> open_files;  // Open files
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


int pfs_initialize();
int pfs_finish(int client_id);
int pfs_create(const char *filename, int stripe_width);
int pfs_open(const char *filename, int mode);
int pfs_read(int fd, void *buf, size_t num_bytes, off_t offset);
int pfs_write(int fd, const void *buf, size_t num_bytes, off_t offset);
int pfs_close(int fd);
int pfs_delete(const char *filename);
int pfs_fstat(int fd, struct pfs_metadata *meta_data);
int pfs_execstat(struct pfs_execstat *execstat_data);
