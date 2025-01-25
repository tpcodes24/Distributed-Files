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
#include <shared_mutex>
#include <mutex>

std::unique_ptr<pfsmeta::MetadataServer::Stub> metadata_stub; // Metadata server stub
std::vector<std::unique_ptr<pfsfile::FileServer::Stub>> file_server_stubs; // File server stubs
std::vector<std::string> file_server_addresses;

static ClientState client_state; // Each client has its own state
static ClientCache client_cache(CLIENT_CACHE_BLOCKS * PFS_BLOCK_SIZE, client_state);



// Forward declarations
int metaserverUp(const std::string& metadata_server_address);
bool metaserverPing(); // Function to ping Metadata Server
bool fileserverPing(const std::string& file_server_address); // Function to ping File Server


int pfs_initialize() {
    std::cout << "[DEBUG] Starting PFS Initialization..." << std::endl;

    // Open pfs_list file to get server addresses
    std::ifstream pfs_list("../pfs_list.txt");
    if (!pfs_list.is_open()) {
        std::cerr << "[ERROR] Cannot open pfs_list.txt file." << std::endl;
        return -1;
    }
    std::cout << "[INFO] pfs_list.txt opened successfully." << std::endl;

    std::string line;
    int line_num = 0;
    int meta_server_client_id = -1;

    while (std::getline(pfs_list, line)) {
        std::string server_address = line.substr(0, line.find(':')) + ":" + line.substr(line.find(':') + 1);
        if (line_num == 0) {
            // Connect to Metadata Server
            std::cout << "[INFO] Connecting to Metadata Server at: " << server_address << std::endl;
            meta_server_client_id = metaserverUp(server_address);
            if (meta_server_client_id < 0) {
                std::cerr << "[ERROR] Failed to initialize connection with Metadata Server." << std::endl;
                return -1;
            }
            client_state.client_id = meta_server_client_id;  // Set Client ID in state
            std::cout << "[INFO] Connected to Metadata Server. Assigned Client ID: " << meta_server_client_id << std::endl;
        } else {
            // Store file server addresses
            std::cout << "[DEBUG] Adding File Server address: " << server_address << std::endl;
            file_server_addresses.push_back(server_address);
        }
        ++line_num;
    }
    pfs_list.close();
    std::cout << "[INFO] Finished reading pfs_list.txt. Total servers: " << file_server_addresses.size() + 1 << std::endl;

    // Ping File Servers
    for (const auto& address : file_server_addresses) {
        std::cout << "[INFO] Pinging File Server at: " << address << std::endl;
        if (!fileserverPing(address)) {
            std::cerr << "[ERROR] Unable to reach File Server at: " << address << std::endl;
            return -1;
        }
        std::cout << "[INFO] File Server at " << address << " is online." << std::endl;
    }

    std::cout << "[DEBUG] PFS Initialization completed successfully." << std::endl;

    // Reset Client State
    {
        std::lock_guard<std::mutex> lock(client_state.state_mutex);
        client_state.open_files.clear();
        client_state.tokens.clear();
        client_state.num_read_hits = 0;
        client_state.num_write_hits = 0;
        client_state.num_evictions = 0;
        client_state.num_writebacks = 0;
        client_state.num_invalidations = 0;
        client_state.num_close_writebacks = 0;
        client_state.num_close_evictions = 0;
    }

    // Initialize Client Cache
    client_cache.initialize();

    std::cout << "[INFO] Client state and cache successfully reset." << std::endl;
    return meta_server_client_id;
}


// Function to connect to the Metadata Server
int metaserverUp(const std::string& metadata_server_address) {
    metadata_stub = pfsmeta::MetadataServer::NewStub(
        grpc::CreateChannel(metadata_server_address, grpc::InsecureChannelCredentials())
    );

    if (!metaserverPing()) {
        std::cerr << "[ERROR] Metadata Server is not responding to Ping." << std::endl;
        return -1;
    }

    // Check if the stub was created successfully
    if (!metadata_stub) {
        std::cerr << "Failed to create metadata_stub!" << std::endl;
        exit(EXIT_FAILURE);
    }

    pfsmeta::InitializeRequest request;
    pfsmeta::InitializeResponse response;
    grpc::ClientContext context;

    grpc::Status status = metadata_stub->Initialize(&context, request, &response);
    if (status.ok()) {
        std::cout << "[INFO] Metadata Server assigned Client ID: " << response.client_id() << std::endl;
        return response.client_id();
    } else {
        std::cerr << "[ERROR] Failed to connect to Metadata Server:\n"
                  << "Error Code: " << status.error_code() << "\n"
                  << "Error Message: " << status.error_message() << std::endl;
        return -1;
    }
}

// Function to ping the Metadata Server
bool metaserverPing() {
    pfsmeta::PingRequest request;
    pfsmeta::PingResponse response;
    grpc::ClientContext context;

    grpc::Status status = metadata_stub->Ping(&context, request, &response);
    if (status.ok() && response.success()) {
        std::cout << "[INFO] Metadata Server Response: " << response.response_message() << std::endl;
        return true;
    } else {
        std::cerr << "[ERROR] Ping to Metadata Server failed:\n"
                  << "Error Code: " << status.error_code() << "\n"
                  << "Error Message: " << status.error_message() << std::endl;
        return false;
    }
}

// Function to ping a File Server
bool fileserverPing(const std::string& file_server_address) {
    std::cout << "[DEBUG] Creating File Server stub for address: " << file_server_address << std::endl;
    auto stub = pfsfile::FileServer::NewStub(
        grpc::CreateChannel(file_server_address, grpc::InsecureChannelCredentials())
    );

    pfsfile::PingRequest request;
    pfsfile::PingResponse response;
    grpc::ClientContext context;

    grpc::Status status = stub->Ping(&context, request, &response);
    if (status.ok() && response.success()) {
        std::cout << "[INFO] File Server at " << file_server_address << " responded: " << response.response_message() << std::endl;
        file_server_stubs.push_back(std::move(stub));
        return true;
    } else {
        std::cerr << "[ERROR] Ping to File Server at " << file_server_address << " failed:\n"
                  << "Error Code: " << status.error_code() << "\n"
                  << "Error Message: " << status.error_message() << std::endl;
        return false;
    }
}


int pfs_finish(int client_id) {
    // Verify the client ID
    if (client_state.client_id != client_id) {
        std::cerr << "[ERROR] Invalid client ID provided to pfs_finish()." << std::endl;
        return -1;
    }

    std::cout << "[INFO] Starting PFS client shutdown for Client ID: " << client_id << std::endl;

    // 1. Close all open files
    {
        std::lock_guard<std::mutex> lock(client_state.state_mutex);
        for (const auto& [fd, file_desc] : client_state.open_files) {
            const std::string& filename = file_desc.filename;

            // Release tokens associated with the file
            grpc::ClientContext context;
            pfsmeta::TokenRequest release_request;
            pfsmeta::TokenResponse release_response;

            release_request.set_client_id(client_id);
            release_request.set_fd(fd);
            release_request.set_filename(filename);
            release_request.set_token_type("CLOSE");

            auto stream = metadata_stub->StreamToken(&context);
            if (stream->Write(release_request)) {
                while (stream->Read(&release_response)) {
                    if (release_response.token_action() == "ACK") {
                        std::cout << "[INFO] Tokens successfully released for file: " << filename << std::endl;
                        break;
                    }
                }
                stream->WritesDone();
            }
            grpc::Status status = stream->Finish();
            if (!status.ok()) {
                std::cerr << "[ERROR] Failed to release tokens for file: " << filename
                          << ". Error: " << status.error_message() << std::endl;
            }
        }

        // Clear open files
        client_state.open_files.clear();
    }

    // 2. Notify the Metadata Server about client shutdown
    grpc::ClientContext context;
    pfsmeta::ClientShutdownRequest shutdown_request;
    pfsmeta::ClientShutdownResponse shutdown_response;

    shutdown_request.set_client_id(client_id);

    grpc::Status shutdown_status = metadata_stub->ClientShutdown(&context, shutdown_request, &shutdown_response);
    if (!shutdown_status.ok() || !shutdown_response.success()) {
        std::cerr << "[ERROR] Metadata Server shutdown notification failed. Error: "
                  << shutdown_status.error_message() << std::endl;
        return -1;
    }

    std::cout << "[INFO] Metadata Server acknowledged client shutdown." << std::endl;

    // 3. Reset client state
    {
        std::lock_guard<std::mutex> lock(client_state.state_mutex);
        client_state.tokens.clear();
        client_state.num_read_hits = 0;
        client_state.num_write_hits = 0;
        client_state.num_evictions = 0;
        client_state.num_writebacks = 0;
        client_state.num_invalidations = 0;
        client_state.num_close_writebacks = 0;
        client_state.num_close_evictions = 0;
    }

    // 4. Clear Metadata and File Server Stubs
    metadata_stub = nullptr;  // Reset metadata stub
    file_server_stubs.clear();  // Clear file server stubs

    std::cout << "[INFO] PFS client shutdown completed successfully for Client ID: " << client_id << std::endl;
    return 0;
}


int pfs_create(const char* filename, int stripe_width) {
    if (!filename || std::strlen(filename) == 0 || stripe_width <= 0 || stripe_width > NUM_FILE_SERVERS) {
        std::cerr << "[ERROR] Invalid arguments to pfs_create()." << std::endl;
        return -1;
    }

    pfsmeta::CreateFileRequest request;
    pfsmeta::CreateFileResponse response;

    request.set_filename(filename);
    request.set_stripe_width(stripe_width);

    grpc::ClientContext context;
    grpc::Status status = metadata_stub->CreateFile(&context, request, &response);

    if (!status.ok()) {
        std::cerr << "[ERROR] gRPC call failed: " << status.error_message() << std::endl;
        return -1;
    }

    if (!response.success()) {
        std::cerr << "[ERROR] CreateFile failed: " << response.message() << std::endl;
        return -1;
    }

    std::cout << "[INFO] File '" << filename << "' created successfully." << std::endl;
    return 0;
}


int pfs_open(const char* filename, int mode) {
    if (!filename || std::strlen(filename) == 0) {
        std::cerr << "[ERROR] Invalid filename provided to pfs_open()." << std::endl;
        return -1;
    }
    if (mode != 1 && mode != 2) {
        std::cerr << "[ERROR] Invalid mode provided to pfs_open(). Mode must be 1 (read) or 2 (read/write)." << std::endl;
        return -1;
    }


    {
        std::lock_guard<std::mutex> lock(client_state.state_mutex);
        for (const auto& [fd, file_desc] : client_state.open_files) {
            if (file_desc.filename == filename) {
                std::cerr << "[ERROR] File '" << filename << "' is already open." << std::endl;
                return -1;
            }
        }
    }


    pfsmeta::FetchMetadataRequest request;
    pfsmeta::FetchMetadataResponse response;
    grpc::ClientContext context;

    request.set_filename(filename);

    grpc::Status status = metadata_stub->FetchMetadata(&context, request, &response);
    if (!status.ok() || !response.success()) {
        std::cerr << "[ERROR] Failed to fetch metadata for file '" << filename
                  << "': " << (response.success() ? response.message() : status.error_message()) << std::endl;
        return -1;
    }


    const auto& metadata = response.metadata();


    if (metadata.filesize() < 0) {
        std::cerr << "[ERROR] Inconsistent metadata for file '" << filename << "'." << std::endl;
        return -1;
    }


    static int next_fd = 1;
    int fd = next_fd++;

 
    {
        std::lock_guard<std::mutex> lock(client_state.state_mutex);
        client_state.open_files[fd] = {filename, mode, metadata.filesize()};
    }

    std::cout << "[INFO] File '" << filename << "' opened successfully with FD " << fd 
              << ". Filesize: " << metadata.filesize() << " bytes." << std::endl;
    return fd;
}


int pfs_read(int fd, void* buf, size_t num_bytes, off_t offset) {
    if (!buf || num_bytes <= 0) {
        std::cerr << "[ERROR] Invalid buffer or size provided to pfs_read()." << std::endl;
        return -1;
    }

    // Validate file descriptor and fetch associated metadata
    std::string filename;
    size_t filesize;
    int mode;
    {
        std::lock_guard<std::mutex> lock(client_state.state_mutex);
        auto it = client_state.open_files.find(fd);
        if (it == client_state.open_files.end()) {
            std::cerr << "[ERROR] Invalid file descriptor: " << fd << std::endl;
            return -1;
        }
        const FileDescriptor& file_desc = it->second;
        filename = file_desc.filename;
        filesize = file_desc.offset;  
        mode = file_desc.mode;
    }

    if (mode != 1 && mode != 2) { 
        std::cerr << "[ERROR] File not opened in read or read/write mode." << std::endl;
        return -1;
    }

    if (offset >= filesize) {
        std::cerr << "[ERROR] Offset is beyond the end of the file." << std::endl;
        return -1;
    }
    num_bytes = std::min(num_bytes, filesize - offset);


    std::cout << "[INFO] Requesting READ token for range [" << offset << ", "
              << offset + num_bytes - 1 << "] for file: " << filename << std::endl;

    grpc::ClientContext context;
    auto stream = metadata_stub->StreamToken(&context);

    pfsmeta::TokenRequest token_request;
    token_request.set_client_id(client_state.client_id);
    token_request.set_fd(fd);
    token_request.set_filename(filename);
    token_request.set_start_byte(offset);
    token_request.set_end_byte(offset + num_bytes - 1);
    token_request.set_token_type("READ");

    stream->Write(token_request);

    // Wait for token grant or handle revocation
    pfsmeta::TokenResponse token_response;
    while (stream->Read(&token_response)) {
        if (token_response.token_action() == "GRANT") {
            std::cout << "[INFO] READ token granted for range [" << token_response.start_byte()
                      << ", " << token_response.end_byte() << "] for file: " << filename << std::endl;
            break;
        } else if (token_response.token_action() == "REVOKE") {
            std::cerr << "[ERROR] Received token revocation during READ request. Aborting read." << std::endl;
            stream->WritesDone();
            return -1;
        }
    }

    stream->WritesDone();
    grpc::Status status = stream->Finish();
    if (!status.ok()) {
        std::cerr << "[ERROR] Communication with Metadata Server failed: "
                  << status.error_message() << std::endl;
        return -1;
    }


    std::cout << "[INFO] Fetching data from file servers for file: " << filename
              << " in range [" << offset << ", " << offset + num_bytes - 1 << "]." << std::endl;

    size_t total_bytes_read = 0;
    size_t remaining_bytes = num_bytes;
    off_t current_offset = offset;
    char* write_ptr = static_cast<char*>(buf);

    while (remaining_bytes > 0) {
        size_t block_offset = current_offset % PFS_BLOCK_SIZE;
        size_t bytes_to_read = std::min(remaining_bytes, PFS_BLOCK_SIZE - block_offset);

        size_t server_index = (current_offset / PFS_BLOCK_SIZE) % file_server_stubs.size();
        auto& file_server_stub = file_server_stubs[server_index];

        pfsfile::ReadFileRequest read_request;
        pfsfile::ReadFileResponse read_response;

        read_request.set_filename(filename);
        read_request.set_offset(current_offset);
        read_request.set_size(bytes_to_read);

        grpc::ClientContext read_context;
        grpc::Status read_status = file_server_stub->ReadFile(&read_context, read_request, &read_response);

        if (!read_status.ok() || !read_response.success()) {
            std::cerr << "[ERROR] File server read failed for file: " << filename
                      << " at offset " << current_offset << ": " << read_status.error_message() << std::endl;
            return -1;
        }

        size_t bytes_read = read_response.data().size();
        if (bytes_read == 0) {
            std::cout << "[INFO] End-of-file reached for file: " << filename << "." << std::endl;
            break;
        }

        std::memcpy(write_ptr, read_response.data().c_str(), bytes_read);

        total_bytes_read += bytes_read;
        remaining_bytes -= bytes_read;
        current_offset += bytes_read;
        write_ptr += bytes_read;
    }

    std::cout << "[INFO] Completed read for file: " << filename << ". Bytes read: "
              << total_bytes_read << "." << std::endl;

    return static_cast<int>(total_bytes_read);
}


int pfs_write(int fd, const void* buf, size_t num_bytes, off_t offset) {
    if (!buf || num_bytes <= 0) {
        std::cerr << "[ERROR] Invalid buffer or size provided to pfs_write()." << std::endl;
        return -1;
    }

    // Validate file descriptor
    std::string filename;
    int mode;
    {
        std::lock_guard<std::mutex> lock(client_state.state_mutex);
        if (client_state.open_files.find(fd) == client_state.open_files.end()) {
            std::cerr << "[ERROR] Invalid file descriptor: " << fd << std::endl;
            return -1;
        }
        filename = client_state.open_files[fd].filename;
        mode = client_state.open_files[fd].mode;
    }

    if (mode != 2) { 
        std::cerr << "[ERROR] File not opened in write mode." << std::endl;
        return -1;
    }

    
    std::cout << "[INFO] Requesting WRITE token for range [" << offset << ", "
              << offset + num_bytes - 1 << "] for file: " << filename << std::endl;

    grpc::ClientContext context;
    auto stream = metadata_stub->StreamToken(&context);

   
    pfsmeta::TokenRequest token_request;
    token_request.set_client_id(client_state.client_id);
    token_request.set_fd(fd);
    token_request.set_filename(filename);
    token_request.set_start_byte(offset);
    token_request.set_end_byte(offset + num_bytes - 1);
    token_request.set_token_type("WRITE");

    stream->Write(token_request);

    // Wait for token grant or handle revocation
    pfsmeta::TokenResponse token_response;
    while (stream->Read(&token_response)) {
        if (token_response.token_action() == "GRANT") {
            std::cout << "[INFO] WRITE token granted for range [" << token_response.start_byte()
                      << ", " << token_response.end_byte() << "] for file: " << filename << std::endl;
            break;
        } else if (token_response.token_action() == "REVOKE") {
            std::cerr << "[ERROR] Received token revocation during WRITE request. Aborting write." << std::endl;
            stream->WritesDone();
            return -1;
        }
    }

    stream->WritesDone();
    grpc::Status status = stream->Finish();
    if (!status.ok()) {
        std::cerr << "[ERROR] Communication with Metadata Server failed: "
                  << status.error_message() << std::endl;
        return -1;
    }

    
    std::cout << "[INFO] Writing data to file servers for file: " << filename
              << " in range [" << offset << ", " << offset + num_bytes - 1 << "]." << std::endl;

    size_t total_bytes_written = 0;
    size_t remaining_bytes = num_bytes;
    off_t current_offset = offset;
    const char* read_ptr = static_cast<const char*>(buf);

    while (remaining_bytes > 0) {
        size_t block_offset = current_offset % PFS_BLOCK_SIZE;
        size_t bytes_to_write = std::min(remaining_bytes, PFS_BLOCK_SIZE - block_offset);

        size_t server_index = (current_offset / PFS_BLOCK_SIZE) % file_server_stubs.size();
        auto& file_server_stub = file_server_stubs[server_index];

       
        pfsfile::WriteFileRequest write_request;
        pfsfile::WriteFileResponse write_response;

        write_request.set_filename(filename);
        write_request.set_offset(current_offset);
        write_request.set_data(std::string(read_ptr, bytes_to_write));

        grpc::ClientContext write_context;
        grpc::Status write_status = file_server_stub->WriteFile(&write_context, write_request, &write_response);

        if (!write_status.ok() || !write_response.success()) {
            std::cerr << "[ERROR] File server write failed for file: " << filename
                      << " at offset " << current_offset << ": " << write_status.error_message() << std::endl;
            return -1;
        }

        total_bytes_written += bytes_to_write;
        remaining_bytes -= bytes_to_write;
        current_offset += bytes_to_write;
        read_ptr += bytes_to_write;
    }

    std::cout << "[INFO] Completed write for file: " << filename << ". Bytes written: "
              << total_bytes_written << "." << std::endl;

    
    grpc::ClientContext metadata_context;
    pfsmeta::UpdateMetadataRequest update_request;
    pfsmeta::UpdateMetadataResponse update_response;

    update_request.set_filename(filename);
    update_request.set_filesize(std::max(static_cast<size_t>(offset + num_bytes), total_bytes_written));
    update_request.set_mtime(std::time(nullptr));  

    grpc::Status metadata_status = metadata_stub->UpdateMetadata(&metadata_context, update_request, &update_response);
    if (!metadata_status.ok() || !update_response.success()) {
        std::cerr << "[ERROR] Failed to update metadata for file: " << filename
                  << ": " << metadata_status.error_message() << std::endl;
        return -1;
    }

    std::cout << "[INFO] Metadata updated successfully for file: " << filename
              << ". New file size: " << update_request.filesize() << "." << std::endl;

    return static_cast<int>(total_bytes_written);
}




int pfs_close(int fd) {
    
    std::string filename;
    {
        std::lock_guard<std::mutex> lock(client_state.state_mutex);
        if (client_state.open_files.find(fd) == client_state.open_files.end()) {
            std::cerr << "[ERROR] Invalid file descriptor: " << fd << std::endl;
            return -1;
        }
        filename = client_state.open_files[fd].filename; 
    }


    std::cout << "[INFO] Releasing tokens for file: " << filename << " and FD: " << fd << std::endl;
    grpc::ClientContext context;
    pfsmeta::TokenRequest release_request;
    pfsmeta::TokenResponse release_response;

    release_request.set_client_id(client_state.client_id);
    release_request.set_fd(fd);
    release_request.set_filename(filename);
    release_request.set_token_type("CLOSE");

  
    auto stream = metadata_stub->StreamToken(&context);


    if (!stream->Write(release_request)) {
        std::cerr << "[ERROR] Failed to send token release request for file: " << filename << std::endl;
        stream->WritesDone();
        return -1;
    }

    // Wait for Metadata Server acknowledgment
    while (stream->Read(&release_response)) {
        if (release_response.token_action() == "ACK") {
            std::cout << "[INFO] Tokens successfully released for file: " << filename << std::endl;
            break;
        } else {
            std::cerr << "[ERROR] Unexpected response during token release for file: " << filename << std::endl;
            stream->WritesDone();
            return -1;
        }
    }

    stream->WritesDone();
    grpc::Status status = stream->Finish(); 
    if (!status.ok()) {
        std::cerr << "[ERROR] Communication with Metadata Server failed during token release: "
                  << status.error_message() << std::endl;
        return -1;
    }

 
    {
        std::lock_guard<std::mutex> lock(client_state.state_mutex);
        client_state.open_files.erase(fd); 
    }

    std::cout << "[INFO] File descriptor " << fd << " for file '" << filename << "' closed successfully." << std::endl;
    return 0;
}



int pfs_delete(const char* filename) {
    if (!filename || std::strlen(filename) == 0) {
        std::cerr << "[ERROR] Invalid filename provided to pfs_delete()." << std::endl;
        return -1;
    }

    // Notify the Metadata Server to delete the file metadata
    std::cout << "[INFO] Requesting metadata server to delete file: " << filename << std::endl;
    grpc::ClientContext context;
    pfsmeta::DeleteFileRequest delete_request;
    pfsmeta::DeleteFileResponse delete_response;

    delete_request.set_filename(filename);

    grpc::Status status = metadata_stub->DeleteFile(&context, delete_request, &delete_response);
    if (!status.ok() || !delete_response.success()) {
        std::cerr << "[ERROR] Metadata server failed to delete file '" << filename
                  << "': " << (delete_response.success() ? delete_response.message() : status.error_message()) << std::endl;
        return -1;
    }

    // Metadata deleted successfully
    std::cout << "[INFO] Metadata server confirmed file deletion. Proceeding to delete physical files." << std::endl;

    for (const auto& file_server_stub : file_server_stubs) {
        grpc::ClientContext fs_context;
        pfsfile::DeleteFileRequest fs_delete_request;
        pfsfile::DeleteFileResponse fs_delete_response;

        fs_delete_request.set_filename(filename);

        grpc::Status fs_status = file_server_stub->DeleteFile(&fs_context, fs_delete_request, &fs_delete_response);
        if (!fs_status.ok() || !fs_delete_response.success()) {
            std::cerr << "[ERROR] Failed to delete file on a file server: " << fs_status.error_message() << std::endl;
            return -1;
        }
    }

    std::cout << "[INFO] File '" << filename << "' successfully deleted from all servers." << std::endl;
    return 0;
}



int pfs_fstat(int fd, struct pfs_metadata *meta_data) {
    std::lock_guard<std::mutex> lock(client_state.state_mutex);


    if (client_state.open_files.find(fd) == client_state.open_files.end()) {
        std::cerr << "[ERROR] Invalid file descriptor: " << fd << std::endl;
        return -1;
    }

    const std::string& filename = client_state.open_files[fd].filename;


    pfsmeta::FetchMetadataRequest request;
    pfsmeta::FetchMetadataResponse response;
    grpc::ClientContext context;

    request.set_filename(filename);

    grpc::Status status = metadata_stub->FetchMetadata(&context, request, &response);
    if (!status.ok() || !response.success()) {
        std::cerr << "[ERROR] Failed to fetch metadata for file '" << filename << "': " << response.message() << std::endl;
        return -1;
    }

 
    const auto& metadata = response.metadata();
    std::strncpy(meta_data->filename, metadata.filename().c_str(), sizeof(meta_data->filename));
    meta_data->file_size = metadata.filesize();
    meta_data->ctime = metadata.ctime();
    meta_data->mtime = metadata.mtime();
    meta_data->recipe.stripe_width = metadata.stripe_width();

    std::cout << "[INFO] Fetched metadata for file '" << filename << "' successfully." << std::endl;
    return 0;
}

int pfs_execstat(struct pfs_execstat *execstat_data) {

    return 0;
}
