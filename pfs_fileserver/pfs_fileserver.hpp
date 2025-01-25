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

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>

#include "pfs_common/pfs_common.hpp"

#include "pfs_proto/pfs_fileserver.pb.h"
#include "pfs_proto/pfs_fileserver.grpc.pb.h"

// // Stub for interacting with the Metadata Server
// extern std::unique_ptr<pfsmeta::MetadataServer::Stub> metadata_stub;

// // Function to initialize the Metadata Server
// int metaserverUp(const std::string& metadata_server_address);

// Function to initialize the Parallel File System
int pfs_initialize();

// // Function to send a ping to the Metadata Server
// bool metaserverPing();
// Vector to hold gRPC stubs for File Servers
extern std::vector<std::unique_ptr<pfsfile::FileServer::Stub>> file_server_stubs;

// Function to send a ping to a File Server and validate connectivity
bool fileserverPing(const std::string& file_server_address);
