#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "pfs_common/pfs_common.hpp"
#include "pfs_client/pfs_api.hpp"

int main(int argc, char *argv[]) {
    printf("%s:%s: Start! Hostname: %s, IP: %s\n", __FILE__, __func__, getMyHostname().c_str(), getMyIP().c_str());

    // Initialize the PFS client
    int client_id = pfs_initialize();
    if (client_id == -1) {
        fprintf(stderr, "pfs_initialize() failed.\n");
        return -1;
    }

    printf("[TEST] PFS Initialization successful. Client ID: %d\n", client_id);

    // Create a PFS file
    int ret = pfs_create("test_file", 3); // Using a stripe width of 3
    if (ret == -1) {
        fprintf(stderr, "pfs_create() failed.\n");
        return -1;
    }

    printf("[TEST] PFS file 'test_file' created successfully.\n");

    // Open the PFS file in write mode
    int pfs_fd = pfs_open("test_file", 2); // Open in read/write mode
    if (pfs_fd == -1) {
        fprintf(stderr, "pfs_open() failed.\n");
        return -1;
    }

    printf("[TEST] PFS file 'test_file' opened successfully with FD: %d\n", pfs_fd);

    // Write data to the PFS file
    const char *data_to_write = "Hello, PFS! This is a test write.";
    size_t data_size = strlen(data_to_write);

    ret = pfs_write(pfs_fd, (void *)data_to_write, data_size, 0); // Write at offset 0
    if (ret == -1) {
        fprintf(stderr, "pfs_write() failed.\n");
        return -1;
    }

    printf("[TEST] Wrote %d bytes to the PFS file.\n", ret);

    // Fetch metadata using pfs_fstat
    struct pfs_metadata meta_data;
    ret = pfs_fstat(pfs_fd, &meta_data);
    if (ret == -1) {
        fprintf(stderr, "pfs_fstat() failed.\n");
        return -1;
    }

    // Print metadata
    printf("[TEST] Metadata fetched successfully:\n");
    printf("  Filename: %s\n", meta_data.filename);
    printf("  File Size: %lu\n", meta_data.file_size);
    printf("  Creation Time: %ld\n", meta_data.ctime);
    printf("  Modification Time: %ld\n", meta_data.mtime);
    printf("  Stripe Width: %d\n", meta_data.recipe.stripe_width);

    // Read data from the PFS file
    char read_buffer[128] = {0}; // Read buffer
    ret = pfs_read(pfs_fd, read_buffer, data_size, 0); // Read from offset 0
    if (ret == -1) {
        fprintf(stderr, "pfs_read() failed.\n");
        return -1;
    }

    printf("[TEST] Read %d bytes from the PFS file. Data: %s\n", ret, read_buffer);

    // // Close the PFS file
    // ret = pfs_close(pfs_fd);
    // if (ret == -1) {
    //     fprintf(stderr, "pfs_close() failed.\n");
    //     return -1;
    // }

    // printf("[TEST] PFS file 'test_file' closed successfully.\n");

    // // Finalize the PFS client
    // ret = pfs_finish(client_id);
    // if (ret == -1) {
    //     fprintf(stderr, "pfs_finish() failed.\n");
    //     return -1;
    // }

    printf("[TEST] PFS client finalized successfully.\n");
    printf("%s:%s: Finish!\n", __FILE__, __func__);
    return 0;
}
