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

static int komanda_test(const char *mnt)
{
    DIR *dir;
    struct dirent *ent;
    char put[PATH_MAX];
    char zapis[64];
    char chtenie[64];
    int fd;
    int chislo;
    int kolvo = 0;

    dir = opendir(mnt);
    if (!dir) {
        perror("opendir");
        return 1;
    }

    srand(time(NULL));

    while ((ent = readdir(dir)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
            continue;

        snprintf(put, sizeof(put), "%s/%s", mnt, ent->d_name);

        chislo = rand();
        snprintf(zapis, sizeof(zapis), "%d\n", chislo);
        memset(chtenie, 0, sizeof(chtenie));

        fd = open(put, O_RDWR);
        if (fd < 0) {
            perror(put);
            continue;
        }

        lseek(fd, 0, SEEK_SET);
        write(fd, zapis, strlen(zapis));

        lseek(fd, 0, SEEK_SET);
        read(fd, chtenie, strlen(zapis));

        close(fd);

        if (strcmp(zapis, chtenie) == 0) {
            printf("OK %s: %d\n", ent->d_name, chislo);
            kolvo++;
        } else {
            printf("FAIL %s\n", ent->d_name);
        }
    }

    closedir(dir);

    printf("Checked files: %d\n", kolvo);

    return 0;
}

static int komanda_zero(const char *put)
{
    int fd = open(put, O_RDWR);

    if (fd < 0) {
        perror("open");
        return 1;
    }

    ioctl(fd, SIMPLEFS_IOCTL_ZERO_ALL);
    close(fd);

    printf("All files zeroed\n");

    return 0;
}

static int komanda_erase(const char *put)
{
    int fd = open(put, O_RDWR);

    if (fd < 0) {
        perror("open");
        return 1;
    }

    ioctl(fd, SIMPLEFS_IOCTL_ERASE_FS);
    close(fd);

    printf("FS erased\n");

    return 0;
}

static int komanda_hashes(const char *put)
{
    int fd;
    unsigned int i;
    unsigned int max_kolvo = 4096;
    struct simplefs_hashes_user req;
    struct simplefs_hash_info *items;

    fd = open(put, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    items = calloc(max_kolvo, sizeof(struct simplefs_hash_info));

    memset(&req, 0, sizeof(req));
    req.max_kolvo = max_kolvo;
    req.user_adres = (u64)(uintptr_t)items;

    ioctl(fd, SIMPLEFS_IOCTL_GET_HASHES, &req);
    close(fd);

    for (i = 0; i < req.real_kolvo && i < max_kolvo; i++) {
        printf("%s: start=%llu size=%u hash=%u\n",
               items[i].imya,
               (unsigned long long)items[i].start_sektor,
               items[i].razmer_v_sektorah,
               items[i].hash);
    }

    free(items);

    return 0;
}

static int komanda_mapping(const char *put, const char *imya)
{
    int fd;
    struct simplefs_mapping_user req;

    fd = open(put, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    memset(&req, 0, sizeof(req));
    snprintf(req.imya, SIMPLEFS_MAX_NAME, "%s", imya);

    ioctl(fd, SIMPLEFS_IOCTL_GET_MAPPING, &req);

    close(fd);

    printf("%s: start=%llu size=%u sectors\n",
           req.imya,
           (unsigned long long)req.start_sektor,
           req.razmer_v_sektorah);

    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        help(argv[0]);
        return 1;
    }

    if (!strcmp(argv[1], "test"))
        return komanda_test(argv[2]);

    if (!strcmp(argv[1], "zero"))
        return komanda_zero(argv[2]);

    if (!strcmp(argv[1], "erase"))
        return komanda_erase(argv[2]);

    if (!strcmp(argv[1], "hashes"))
        return komanda_hashes(argv[2]);

    if (!strcmp(argv[1], "mapping") && argc == 4)
        return komanda_mapping(argv[2], argv[3]);

    help(argv[0]);

    return 1;
}