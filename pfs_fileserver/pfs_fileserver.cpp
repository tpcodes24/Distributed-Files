#include <filesystem>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <iostream>
#include "pfs_fileserver.hpp"
#include "pfs_proto/pfs_fileserver.pb.h"
#include "pfs_proto/pfs_fileserver.grpc.pb.h"
#include <grpcpp/grpcpp.h>

namespace fs = std::filesystem;

class FileServerServiceImpl final : public pfsfile::FileServer::Service {
public:
    FileServerServiceImpl() {
        std::cout << "[INFO] FileServerServiceImpl initialized." << std::endl;

        // Ensure the storage directory exists
        if (!fs::exists("./pfs_storage")) {
            std::cout << "[INFO] Creating storage directory: ./pfs_storage" << std::endl;
            fs::create_directory("./pfs_storage");
        }
    }

    virtual ~FileServerServiceImpl() {
        std::cout << "[INFO] FileServerServiceImpl destroyed." << std::endl;
    }

    grpc::Status Ping(grpc::ServerContext* context,
                      const pfsfile::PingRequest* request,
                      pfsfile::PingResponse* response) override {
        std::cout << "[DEBUG] Ping received from: " << context->peer() << std::endl;

        response->set_success(true);
        response->set_response_message("File Server is alive");
        return grpc::Status::OK;
    }

    grpc::Status StreamData(
        grpc::ServerContext* context,
        grpc::ServerReaderWriter<pfsfile::StreamResponse, pfsfile::StreamRequest>* stream) override {
        
        pfsfile::StreamRequest request;
        while (stream->Read(&request)) {
            std::string client_id = request.client_id();
            std::string operation = request.operation();
            std::string filename = "./pfs_storage/" + request.filename();
            int64_t offset = request.offset();
            const std::string& data = request.data();
            int64_t size = request.size();

            pfsfile::StreamResponse response;
            response.set_client_id(client_id);
            response.set_filename(request.filename());

            if (operation == "READ") {
                handle_read(filename, offset, size, response);
            } else if (operation == "WRITE") {
                handle_write(filename, offset, data, response);
            } else {
                response.set_success(false);
                response.set_error_message("Invalid operation: " + operation);
            }

            stream->Write(response);
        }
        return grpc::Status::OK;
    }

    grpc::Status WriteFile(grpc::ServerContext* context,
                                              const pfsfile::WriteFileRequest* request,
                                              pfsfile::WriteFileResponse* response) {
    std::lock_guard<std::mutex> lock(file_mutex);

    const std::string& filename = "./pfs_storage/" + request->filename();
    int64_t offset = request->offset();
    const std::string& data = request->data();


    if (!fs::exists("./pfs_storage")) {
        fs::create_directory("./pfs_storage");
    }

    std::fstream file(filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) {

        file.open(filename, std::ios::out | std::ios::binary | std::ios_base::trunc);
        file.close();


        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            response->set_success(false);
            response->set_error_message("Failed to create file");
            return grpc::Status::OK;
        }
    }


    file.seekp(0, std::ios::end);
    int64_t file_size = file.tellp();
    if (offset + static_cast<int64_t>(data.size()) > file_size) {
        file.close();
        std::ofstream resize_file(filename, std::ios::binary | std::ios_base::app);
        resize_file.seekp(offset + static_cast<int64_t>(data.size()) - 1);
        resize_file.put('\0');
        resize_file.close();
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    }

    // Write the data
    file.seekp(offset, std::ios::beg);
    file.write(data.c_str(), data.size());
    if (!file.good()) {
        file.close();
        response->set_success(false);
        response->set_error_message("Failed to write data");
        return grpc::Status::OK;
    }

    file.close();
    response->set_success(true);
    return grpc::Status::OK;
}


grpc::Status ReadFile(
    grpc::ServerContext* context,
    const pfsfile::ReadFileRequest* request,
    pfsfile::ReadFileResponse* response) {
    std::lock_guard<std::mutex> lock(file_mutex);

    std::string filename = "./pfs_storage/" + request->filename();
    int64_t offset = request->offset();
    int64_t size = request->size();

    // Check if the file exists
    if (!fs::exists(filename)) {
        response->set_success(false);
        response->set_error_message("File not found");
        return grpc::Status::OK;
    }

    // Open the file in binary mode for reading
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        response->set_success(false);
        response->set_error_message("Failed to open file for reading");
        return grpc::Status::OK;
    }

    // Seek to the offset
    file.seekg(offset, std::ios::beg);
    if (!file.good()) {
        response->set_success(false);
        response->set_error_message("Invalid offset");
        file.close();
        return grpc::Status::OK;
    }

    // Read the requested data
    std::vector<char> buffer(size);
    file.read(buffer.data(), size);
    std::streamsize bytes_read = file.gcount();
    file.close();

    if (bytes_read > 0) {
        response->set_success(true);
        response->set_data(buffer.data(), bytes_read);
    } else {
        response->set_success(false);
        response->set_error_message("End of file or no data read");
    }
    return grpc::Status::OK;
}



private:
    std::mutex file_mutex; // Mutex to protect file access

    void handle_read(const std::string& filename, int64_t offset, int64_t size, pfsfile::StreamResponse& response) {
        std::lock_guard<std::mutex> lock(file_mutex);

        // Check if the file exists
        if (!fs::exists(filename)) {
            response.set_success(false);
            response.set_error_message("File not found");
            return;
        }


        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            response.set_success(false);
            response.set_error_message("Failed to open file for reading");
            return;
        }


        file.seekg(offset, std::ios::beg);
        if (!file.good()) {
            response.set_success(false);
            response.set_error_message("Invalid offset");
            file.close();
            return;
        }


        std::vector<char> buffer(size);
        file.read(buffer.data(), size);
        std::streamsize bytes_read = file.gcount();
        file.close();

        if (bytes_read > 0) {
            response.set_success(true);
            response.set_data(buffer.data(), bytes_read);
        } else {
            response.set_success(false);
            response.set_error_message("End of file or no data read");
        }
    }

    void handle_write(const std::string& filename, int64_t offset, 
                                         const std::string& data, pfsfile::StreamResponse& response) {
    std::lock_guard<std::mutex> lock(file_mutex);


    if (!fs::exists("./pfs_storage")) {
        fs::create_directory("./pfs_storage");
    }

    // Open file for reading and writing
    std::fstream file(filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) {

        file.open(filename, std::ios::out | std::ios::binary | std::ios_base::trunc);
        file.close();

        // Reopen in read/write mode
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            response.set_success(false);
            response.set_error_message("Failed to create file");
            return;
        }
    }

    // Resize the file if needed
    file.seekp(0, std::ios::end);
    int64_t file_size = file.tellp();
    if (offset + static_cast<int64_t>(data.size()) > file_size) {
        file.close();
        std::ofstream resize_file(filename, std::ios::binary | std::ios_base::app);
        resize_file.seekp(offset + static_cast<int64_t>(data.size()) - 1);
        resize_file.put('\0');
        resize_file.close();
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    }

    // Write the data
    file.seekp(offset, std::ios::beg);
    file.write(data.c_str(), data.size());
    if (!file.good()) {
        file.close();
        response.set_success(false);
        response.set_error_message("Failed to write data");
        return;
    }

    file.close();
    response.set_success(true);
}

grpc::Status DeleteFile(grpc::ServerContext* context,
                        const pfsfile::DeleteFileRequest* request,
                        pfsfile::DeleteFileResponse* response) override {
    const std::string filename = "./pfs_storage/" + request->filename();

    // Check if the file exists
    if (!fs::exists(filename)) {
        response->set_success(false);
        response->set_message("File not found.");
        return grpc::Status::OK;
    }

    // Attempt to delete the file
    try {
        fs::remove(filename);
        response->set_success(true);
        std::cout << "[INFO] File '" << filename << "' deleted successfully from storage." << std::endl;
    } catch (const std::exception& e) {
        response->set_success(false);
        response->set_message("Failed to delete file: " + std::string(e.what()));
        std::cerr << "[ERROR] Failed to delete file '" << filename << "': " << e.what() << std::endl;
    }

    return grpc::Status::OK;
}

};

int main(int argc, char* argv[]) {
    printf("%s:%s: PFS file server start! Hostname: %s, IP: %s\n",
           __FILE__, __func__, getMyHostname().c_str(), getMyIP().c_str());

    // Parse pfs_list.txt
    std::ifstream pfs_list("../pfs_list.txt");
    if (!pfs_list.is_open()) {
        fprintf(stderr, "%s: can't open pfs_list.txt file.\n", __func__);
        exit(EXIT_FAILURE);
    }

    bool found = false;
    std::string line;
    std::getline(pfs_list, line); // First line is the meta server
    while (std::getline(pfs_list, line)) {
        if (line.substr(0, line.find(':')) == getMyHostname()) {
            found = true;
            break;
        }
    }
    if (!found) {
        fprintf(stderr, "%s: hostname not found in pfs_list.txt.\n", __func__);
        exit(EXIT_FAILURE);
    }
    pfs_list.close();

    std::string listen_port = line.substr(line.find(':') + 1);
    std::string server_address = "0.0.0.0:" + listen_port;

    std::cout << "[INFO] File Server will listen at: " << server_address << std::endl;

    // Start the File Server
    FileServerServiceImpl service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (!server) {
        std::cerr << "[ERROR] Failed to start File Server!" << std::endl;
        return -1;
    }

    std::cout << "[INFO] File Server is running at: " << server_address << std::endl;

    // Wait for incoming requests
    server->Wait();

    std::cout << "[INFO] File Server shutting down..." << std::endl;
    return 0;
}
