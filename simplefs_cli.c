#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/ioctl.h>
#include <limits.h>

#include "simplefs_ioctl.h"

static void help(const char *prog)
{
    printf("Usage:\n");
    printf("  %s test <mount_dir>\n", prog);
    printf("  %s zero <file>\n", prog);
    printf("  %s erase <file>\n", prog);
    printf("  %s hashes <file>\n", prog);
    printf("  %s mapping <file> <name>\n", prog);
}

static int command_test(const char *mnt)
{
    DIR *dir;
    struct dirent *ent;
    char path[PATH_MAX];
    char write_buf[64];
    char read_buf[64];
    int fd;
    int number;
    int count = 0;

    dir = opendir(mnt);
    if (!dir) {
        perror("opendir");
        return 1;
    }

    srand(time(NULL));

    while ((ent = readdir(dir)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
            continue;

        snprintf(path, sizeof(path), "%s/%s", mnt, ent->d_name);

        number = rand();
        snprintf(write_buf, sizeof(write_buf), "%d\n", number);
        memset(read_buf, 0, sizeof(read_buf));

        fd = open(path, O_RDWR);
        if (fd < 0) {
            perror(path);
            continue;
        }

        lseek(fd, 0, SEEK_SET);
        write(fd, write_buf, strlen(write_buf));

        lseek(fd, 0, SEEK_SET);
        read(fd, read_buf, strlen(write_buf));

        close(fd);

        if (strcmp(write_buf, read_buf) == 0) {
            printf("OK %s: %d\n", ent->d_name, number);
            count++;
        } else {
            printf("FAIL %s\n", ent->d_name);
        }
    }

    closedir(dir);

    printf("Checked files: %d\n", count);

    return 0;
}

static int command_zero(const char *path)
{
    int fd = open(path, O_RDWR);

    if (fd < 0) {
        perror("open");
        return 1;
    }

    ioctl(fd, SIMPLEFS_IOCTL_ZERO_ALL);
    close(fd);

    printf("All files zeroed\n");

    return 0;
}

static int command_erase(const char *path)
{
    int fd = open(path, O_RDWR);

    if (fd < 0) {
        perror("open");
        return 1;
    }

    ioctl(fd, SIMPLEFS_IOCTL_ERASE_FS);
    close(fd);

    printf("FS erased\n");

    return 0;
}

static int command_hashes(const char *path)
{
    int fd;
    unsigned int i;
    unsigned int max_count = 4096;
    struct simplefs_hashes_user req;
    struct simplefs_hash_info *items;

    fd = open(path, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    items = calloc(max_count, sizeof(struct simplefs_hash_info));

    memset(&req, 0, sizeof(req));
    req.max_count = max_count;
    req.user_addr = (u64)(uintptr_t)items;

    ioctl(fd, SIMPLEFS_IOCTL_GET_HASHES, &req);
    close(fd);

    for (i = 0; i < req.real_count && i < max_count; i++) {
        printf("%s: start=%llu size=%u hash=%u\n",
               items[i].name,
               (unsigned long long)items[i].start_sector,
               items[i].sectors,
               items[i].hash);
    }

    free(items);

    return 0;
}

static int command_mapping(const char *path, const char *name)
{
    int fd;
    struct simplefs_mapping_user req;

    fd = open(path, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    memset(&req, 0, sizeof(req));
    snprintf(req.name, SIMPLEFS_MAX_NAME, "%s", name);

    ioctl(fd, SIMPLEFS_IOCTL_GET_MAPPING, &req);

    close(fd);

    printf("%s: start=%llu size=%u sectors\n",
           req.name,
           (unsigned long long)req.start_sector,
           req.sectors);

    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        help(argv[0]);
        return 1;
    }

    if (!strcmp(argv[1], "test"))
        return command_test(argv[2]);

    if (!strcmp(argv[1], "zero"))
        return command_zero(argv[2]);

    if (!strcmp(argv[1], "erase"))
        return command_erase(argv[2]);

    if (!strcmp(argv[1], "hashes"))
        return command_hashes(argv[2]);

    if (!strcmp(argv[1], "mapping") && argc == 4)
        return command_mapping(argv[2], argv[3]);

    help(argv[0]);

    return 1;
}