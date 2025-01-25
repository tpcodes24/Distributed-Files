# Distributed-Files-System
  

This project implements a **Distributed File System** that operates across multiple servers, leveraging **gRPC** for fast and reliable communication. The system ensures synchronized access to files, maintains metadata, and provides consistent file operations for multiple clients in a parallel environment.

## Key Features  
### 1. **Token-Based Synchronization**  
The system uses **tokens** to define access permissions for specific byte ranges of files. This ensures:  
- **Sequential Consistency**: Prevents overlapping operations on file byte ranges.  
- **Efficient Synchronization**: Grants or revokes tokens dynamically based on client requests.  

### 2. **Metadata Management**  
A **Metadata Server** maintains critical file metadata, such as:  
- File structure and recipes.  
- Data chunk locations.  
- Client-specific access tokens.  

### 3. **Reliable Communication with gRPC**  
The use of **gRPC** ensures fast and reliable interaction between clients and servers, enabling:  
- Streamlined tokenization and metadata updates.  
- High-performance parallel file access.  

## Implementation Details  
The PFS is built around three key data structures:  
- **FileDescriptor**: Tracks open files, access modes, offsets, and sizes for each client.  
- **Token**: Manages client-specific access permissions to file byte ranges.  
- **ClientState**: Tracks a client’s open files, granted tokens, and performance metrics.  

### Core Components and Functions  
#### 1. **Tokenization and Access Control**  
- `StreamToken`: Handles client token requests, preventing conflicts via tokenization.  
- `split_tokens`: Splits tokens to resolve overlaps.  
- `consolidate_token_ranges`: Merges token ranges for efficient management.  
- `grant_tokens`: Grants access to requested file byte ranges.  
- `revoke_tokens_and_wait_for_ack`: Revokes conflicting tokens and ensures acknowledgment from clients.  

#### 2. **Metadata Management**  
- `CreateFile`: Creates metadata for new files, validating parameters like stripe width and populating file recipes.  
- `UpdateMetadata`: Updates file size and modification timestamps.  

## Getting Started  
### Prerequisites  
- gRPC installed on your system.  
- C++/Python compiler (depending on implementation language).  

### Running the System  
1. **Start the Metadata Server:**  
   ```bash
   ./metadata_server
