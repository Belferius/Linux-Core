#ifndef SIMPLEFS_IOCTL_H
#define SIMPLEFS_IOCTL_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
#else
#include <sys/ioctl.h>
#include <stdint.h>
typedef uint32_t u32;
typedef uint64_t u64;
#endif

#define SIMPLEFS_MAX_NAME 64
#define SIMPLEFS_IOCTL_MAGIC 's'

struct simplefs_hash_info {
    char name[SIMPLEFS_MAX_NAME];
    u64 start_sector;
    u32 sectors;
    u32 hash;
};

struct simplefs_hashes_user {
    u32 max_count;
    u32 real_count;
    u64 user_addr;
};

struct simplefs_mapping_user {
    char name[SIMPLEFS_MAX_NAME];
    u64 start_sector;
    u32 sectors;
};

#define SIMPLEFS_IOCTL_ZERO_ALL    _IO(SIMPLEFS_IOCTL_MAGIC, 1)
#define SIMPLEFS_IOCTL_ERASE_FS    _IO(SIMPLEFS_IOCTL_MAGIC, 2)
#define SIMPLEFS_IOCTL_GET_HASHES  _IOWR(SIMPLEFS_IOCTL_MAGIC, 3, struct simplefs_hashes_user)
#define SIMPLEFS_IOCTL_GET_MAPPING _IOWR(SIMPLEFS_IOCTL_MAGIC, 4, struct simplefs_mapping_user)

#endif
