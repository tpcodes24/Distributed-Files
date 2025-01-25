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
    if (argc < 2) {
        fprintf(stderr, "%s: usage: ./sample1-1 <input filename>\n", __func__);
        return -1;
    }

    // Open a file
    std::string input_filename(argv[1]);
    int input_fd = open(input_filename.c_str(), O_RDONLY);
    char *buf = (char *)malloc(8 * 1024);
    ssize_t nread = pread(input_fd, (void *)buf, 1024, 0);
    if (nread != 1024) {
        fprintf(stderr, "pread() error.\n");
        return -1;
    }
    close(input_fd);

    //Initialize the PFS client
    int client_id = pfs_initialize();
    if (client_id == -1) {
        fprintf(stderr, "pfs_initialize() failed.\n");
        return -1;
    }

    // Create a PFS file
    int ret;
    ret = pfs_create("pfs_file1", 3);
    if (ret == -1) {
        fprintf(stderr, "Unable to create a PFS file.\n");
        return -1;
    }

    // Open the PFS file in write mode
    int pfs_fd = pfs_open("pfs_file1", 2);
    if (pfs_fd == -1) {
        fprintf(stderr, "Error opening PFS file.\n");
        return -1;
    }


        // Test pfs_fstat
    struct pfs_metadata meta_data;
    ret = pfs_fstat(pfs_fd, &meta_data);
    if (ret == -1) {
        fprintf(stderr, "pfs_fstat() failed.\n");
        return -1;
    }

    // Print the fetched metadata
    printf("File Metadata:\n");
    printf("  Filename: %s\n", meta_data.filename);
    printf("  File Size: %lu\n", meta_data.file_size);
    printf("  Creation Time: %ld\n", meta_data.ctime);
    printf("  Modification Time: %ld\n", meta_data.mtime);
    printf("  Stripe Width: %d\n", meta_data.recipe.stripe_width);

    // Write the byte 0~1023 to pfs_file1 at offset 0
    ret = pfs_write(pfs_fd, (void *)buf, 1024, 0);
    if (ret == -1) {
        fprintf(stderr, "Write error to PFS file.\n");
        return -1;
    } else
        printf("%s:%s: Wrote %d bytes to the PFS file.\n", __FILE__, __func__, ret);

    ret = pfs_close(pfs_fd);
    if (ret == -1) {
        fprintf(stderr, "Error closing PFS file.\n");
        return -1;
    }

    ret = pfs_finish(client_id);
    if (ret == -1) {
        fprintf(stderr, "pfs_finish() failed.\n");
        return -1;
    }

    free(buf);
    printf("%s:%s: Finish!\n", __FILE__, __func__);
    return 0;
}
