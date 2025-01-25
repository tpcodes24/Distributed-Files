#include "pfs_metaserver.hpp"
#include "pfs_proto/pfs_metaserver.pb.h"
#include "pfs_proto/pfs_metaserver.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <shared_mutex>


class MetadataServerServiceImpl final : public pfsmeta::MetadataServer::Service {
public:
    std::shared_mutex metadata_mutex;  
    std::unordered_map<std::string, pfsmeta::FileMetadata> file_metadata_map; 
    std::shared_mutex token_table_mutex; 
    std::unordered_map<std::string, std::vector<Token>> token_table; 
    std::mutex revoke_mutex; 
    std::vector<Token> revoke_tokens; 
    //std::unordered_map<int, FileDescriptor> open_files;  

    MetadataServerServiceImpl() {
        std::cout << "[INFO] MetadataServerServiceImpl initialized." << std::endl;
    }

    virtual ~MetadataServerServiceImpl() {
        std::cout << "[INFO] MetadataServerServiceImpl destroyed." << std::endl;
    }

    grpc::Status Initialize(grpc::ServerContext* context,
                            const pfsmeta::InitializeRequest* request,
                            pfsmeta::InitializeResponse* response) override {
        std::cout << "[DEBUG] Received Initialize request from: " << context->peer() << std::endl;

        static int client_id_counter = 1;
        response->set_client_id(client_id_counter++);

        std::cout << "[DEBUG] Assigned Client ID: " << response->client_id()
                  << " to client at " << context->peer() << std::endl;
        return grpc::Status::OK;
    }

    grpc::Status Ping(grpc::ServerContext* context,
                      const pfsmeta::PingRequest* request,
                      pfsmeta::PingResponse* response) override {
        std::cout << "[DEBUG] Ping received from: " << context->peer() << std::endl;

        response->set_success(true);
        response->set_response_message("Metadata Server is alive");

        std::cout << "[INFO] Responded to Ping from: " << context->peer() << " with success." << std::endl;
        return grpc::Status::OK;
    }

    grpc::Status CreateFile(grpc::ServerContext* context,
                            const pfsmeta::CreateFileRequest* request,
                            pfsmeta::CreateFileResponse* response) override {
        const std::string& filename = request->filename();
        int stripe_width = request->stripe_width();


        std::unique_lock<std::shared_mutex> lock(metadata_mutex);

        if (filename.empty() || stripe_width <= 0 || stripe_width > NUM_FILE_SERVERS) {
            response->set_success(false);
            response->set_message("Invalid filename or stripe width.");
            return grpc::Status::OK;
        }

        if (file_metadata_map.find(filename) != file_metadata_map.end()) {
            response->set_success(false);
            response->set_message("File already exists.");
            return grpc::Status::OK;
        }


        pfsmeta::FileMetadata metadata;
        metadata.set_filename(filename);
        metadata.set_filesize(0); // File size is 0 at creation
        metadata.set_ctime(std::time(nullptr));
        metadata.set_mtime(0); // Not closed yet
        metadata.set_stripe_width(stripe_width);

  
        populate_file_recipes(metadata, stripe_width);

        file_metadata_map[filename] = metadata;

        response->set_success(true);
        response->set_message("File created successfully.");
        std::cout << "[INFO] File '" << filename << "' created with stripe width " << stripe_width << "." << std::endl;

        return grpc::Status::OK;
    }

    grpc::Status FetchMetadata(grpc::ServerContext* context,
                               const pfsmeta::FetchMetadataRequest* request,
                               pfsmeta::FetchMetadataResponse* response) override {
        const std::string& filename = request->filename();

        std::shared_lock<std::shared_mutex> lock(metadata_mutex);

        auto it = file_metadata_map.find(filename);
        if (it == file_metadata_map.end()) {
            response->set_success(false);
            response->set_message("File not found.");
            return grpc::Status::OK;
        }

        response->set_success(true);
        response->mutable_metadata()->CopyFrom(it->second);
        std::cout << "[INFO] Metadata fetched for file: " << filename << std::endl;

        return grpc::Status::OK;
    }

    grpc::Status StreamToken(grpc::ServerContext* context,
                             grpc::ServerReaderWriter<pfsmeta::TokenResponse, pfsmeta::TokenRequest>* stream) override {
        pfsmeta::TokenRequest request;
        while (stream->Read(&request)) {
            int client_id = request.client_id();
            int fd = request.fd();
            std::string filename = request.filename();
            int64_t start_byte = request.start_byte();
            int64_t end_byte = request.end_byte();
            int token_type = (request.token_type() == "READ") ? 1 : 2;

            std::string token_close = request.token_type();

        if (token_close == "CLOSE") {

            std::cout << "[INFO] Client " << client_id << " requested to close file: " << filename << std::endl;


            release_tokens(client_id, filename);
            pfsmeta::TokenResponse response;
            response.set_client_id(client_id);
            response.set_filename(filename);
            response.set_token_action("ACK");
            stream->Write(response);

            std::cout << "[INFO] File: " << filename << " successfully closed for client_id: " << client_id << std::endl;
            continue; 
        }

            Token requested_token{client_id, -1, filename, token_type, start_byte, end_byte};
            std::vector<Token> granted_tokens, conflicting_tokens;

            {
                std::unique_lock<std::shared_mutex> lock(token_table_mutex);
                auto& tokens = token_table[filename];

                split_tokens(tokens, requested_token, conflicting_tokens);
                consolidate_token_ranges(tokens, requested_token, granted_tokens);
            }

            grant_tokens(stream, granted_tokens);
            revoke_tokens_and_wait_for_ack(stream, conflicting_tokens);
        }
        return grpc::Status::OK;
    }
    void release_tokens(int client_id, const std::string& filename) {
        std::unique_lock<std::shared_mutex> lock(token_table_mutex);
        auto& tokens = token_table[filename];
        tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                                    [client_id](const Token& token) { return token.client_id == client_id; }),
                     tokens.end());
    }

    grpc::Status UpdateMetadata(grpc::ServerContext* context,
                            const pfsmeta::UpdateMetadataRequest* request,
                            pfsmeta::UpdateMetadataResponse* response) override {
    const std::string& filename = request->filename();
    int64_t new_filesize = request->filesize();
    int64_t new_mtime = request->mtime(); 


    std::unique_lock<std::shared_mutex> lock(metadata_mutex);

    auto it = file_metadata_map.find(filename);
    if (it == file_metadata_map.end()) {
        response->set_success(false);
        response->set_message("File not found.");
        return grpc::Status::OK;
    }
    auto& metadata = it->second;
    metadata.set_filesize(std::max(metadata.filesize(), new_filesize));
    if (new_mtime > 0) {
        metadata.set_mtime(new_mtime);
    }

    response->set_success(true);
    response->set_message("Metadata updated successfully.");
    std::cout << "[INFO] Metadata updated for file: " << filename
              << ". New filesize: " << metadata.filesize()
              << ", mtime: " << metadata.mtime() << "." << std::endl;

    return grpc::Status::OK;
}


    void populate_file_recipes(pfsmeta::FileMetadata& metadata, int stripe_width) {
        int64_t range_start = 0;
        int64_t stripe_size = PFS_BLOCK_SIZE; // Assume block size for each stripe

        for (int i = 0; i < stripe_width; ++i) {
            auto* recipe = metadata.add_recipes();
            recipe->set_file_server_address("FileServer-" + std::to_string(i));
            recipe->set_range_start(range_start);
            recipe->set_range_end(range_start + stripe_size - 1);
            range_start += stripe_size;
        }
    }
    bool conflicts_with_existing_token(const Token& existing, const Token& requested) {
        return existing.filename == requested.filename &&
               existing.end_byte >= requested.start_byte &&
               existing.start_byte <= requested.end_byte &&
               (existing.token_type == 2 || requested.token_type == 2); // WRITE conflicts with READ/WRITE
    }
    void split_tokens(std::vector<Token>& tokens, const Token& requested, std::vector<Token>& conflicting_tokens) {
        for (auto it = tokens.begin(); it != tokens.end();) {
            Token& existing = *it;
            if (conflicts_with_existing_token(existing, requested)) {
                conflicting_tokens.push_back(existing);

                it = tokens.erase(it);

                if (existing.start_byte < requested.start_byte) {
                    tokens.push_back({existing.client_id, existing.fd, existing.filename, existing.token_type,
                                      existing.start_byte, requested.start_byte - 1});
                }
                if (existing.end_byte > requested.end_byte) {
                    tokens.push_back({existing.client_id, existing.fd, existing.filename, existing.token_type,
                                      requested.end_byte + 1, existing.end_byte});
                }
            } else {
                ++it;
            }
        }
    }
    void consolidate_token_ranges(std::vector<Token>& tokens, const Token& new_token,
                                   std::vector<Token>& granted_tokens) {
        for (auto& token : tokens) {
            if (token.token_type == new_token.token_type && token.filename == new_token.filename &&
                token.end_byte >= new_token.start_byte && token.start_byte <= new_token.end_byte) {
                token.start_byte = std::min(token.start_byte, new_token.start_byte);
                token.end_byte = std::max(token.end_byte, new_token.end_byte);
                granted_tokens.push_back(token);
                return;
            }
        }
        tokens.push_back(new_token);
        granted_tokens.push_back(new_token);
    }
    void grant_tokens(grpc::ServerReaderWriter<pfsmeta::TokenResponse, pfsmeta::TokenRequest>* stream,
                      const std::vector<Token>& granted_tokens) {
        for (const auto& token : granted_tokens) {
            pfsmeta::TokenResponse response;
            response.set_client_id(token.client_id);
            response.set_filename(token.filename);
            response.set_start_byte(token.start_byte);
            response.set_end_byte(token.end_byte);
            response.set_token_action("GRANT");

            stream->Write(response);
        }
    }

    void revoke_tokens_and_wait_for_ack(
        grpc::ServerReaderWriter<pfsmeta::TokenResponse, pfsmeta::TokenRequest>* stream,
        const std::vector<Token>& conflicting_tokens) {
        for (const auto& token : conflicting_tokens) {
            pfsmeta::TokenResponse revoke_msg;
            revoke_msg.set_client_id(token.client_id);
            revoke_msg.set_filename(token.filename);
            revoke_msg.set_start_byte(token.start_byte);
            revoke_msg.set_end_byte(token.end_byte);
            revoke_msg.set_token_action("REVOKE");

            stream->Write(revoke_msg);
        }

        while (!conflicting_tokens.empty()) {
            pfsmeta::TokenRequest ack_request;
            if (stream->Read(&ack_request) && ack_request.token_type() == "ACK") {
                int64_t ack_start = ack_request.start_byte();
                int64_t ack_end = ack_request.end_byte();

                std::lock_guard<std::mutex> lock(revoke_mutex);
                revoke_tokens.erase(std::remove_if(revoke_tokens.begin(), revoke_tokens.end(),
                                                   [&](const Token& token) {
                                                       return token.start_byte == ack_start &&
                                                              token.end_byte == ack_end;
                                                   }),
                                    revoke_tokens.end());
            }
        }
    }



grpc::Status DeleteFile(grpc::ServerContext* context,
                        const pfsmeta::DeleteFileRequest* request,
                        pfsmeta::DeleteFileResponse* response) override {
    const std::string& filename = request->filename();

    std::unique_lock<std::shared_mutex> lock(metadata_mutex);
    auto it = file_metadata_map.find(filename);
    if (it == file_metadata_map.end()) {
        response->set_success(false);
        response->set_message("File not found.");
        return grpc::Status::OK;
    }

    {
        std::unique_lock<std::shared_mutex> token_lock(token_table_mutex);
        if (!token_table[filename].empty()) {
            response->set_success(false);
            response->set_message("File is locked by active tokens.");
            return grpc::Status::OK;
        }
    }

    file_metadata_map.erase(it);

    response->set_success(true);
    response->set_message("File metadata deleted successfully.");
    std::cout << "[INFO] Metadata for file '" << filename << "' deleted successfully." << std::endl;
    return grpc::Status::OK;
}

grpc::Status ClientShutdown(
    grpc::ServerContext* context,
    const pfsmeta::ClientShutdownRequest* request,
    pfsmeta::ClientShutdownResponse* response) {

    int client_id = request->client_id();
    std::cout << "[INFO] Received shutdown request from Client ID: " << client_id << std::endl;

    response->set_success(true);
    response->set_message("Client shutdown acknowledged.");
    std::cout << "[INFO] Client ID: " << client_id << " successfully shut down." << std::endl;

    return grpc::Status::OK;
}
};


int main(int argc, char* argv[]) {
    std::cout << "[INFO] Starting PFS Metadata Server..." << std::endl;

    std::ifstream pfs_list("../pfs_list.txt");
    if (!pfs_list.is_open()) {
        std::cerr << "[ERROR] Unable to open pfs_list.txt file." << std::endl;
        return -1;
    }

    std::string line;
    std::getline(pfs_list, line);
    pfs_list.close();

    if (line.substr(0, line.find(':')) != getMyHostname()) {
        std::cerr << "[ERROR] Hostname not on the first line of pfs_list.txt." << std::endl;
        return -1;
    }

    std::string listen_port = line.substr(line.find(':') + 1);
    std::string server_address = "0.0.0.0:" + listen_port;

    std::cout << "[INFO] Metadata Server will listen at: " << server_address << std::endl;

    MetadataServerServiceImpl service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (!server) {
        std::cerr << "[ERROR] Failed to start Metadata Server!" << std::endl;
        return -1;
    }

    std::cout << "[INFO] Metadata Server is running at: " << server_address << std::endl;

    std::thread log_tokens([&service]() {
        while (true) {
            {
                std::shared_lock<std::shared_mutex> lock(service.token_table_mutex);
                std::cout << "[DEBUG] Current Active Tokens:\n";
                for (const auto& [filename, tokens] : service.token_table) {
                    std::cout << "  File: " << filename << "\n";
                    for (const auto& token : tokens) {
                        std::cout << "    [" << token.start_byte << ", " << token.end_byte
                                  << "] (" << (token.token_type == 1 ? "READ" : "WRITE") << "), Client: " << token.client_id << "\n";
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(10)); 
        }
    }
    
    );


    server->Wait();
    std::cout << "[INFO] Metadata Server shutting down..." << std::endl;
    return 0;
}
