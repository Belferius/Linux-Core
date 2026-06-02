#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mnt_idmapping.h>
#include <linux/uaccess.h>
#include "simplefs_ioctl.h"


//  Константы, параметры и структуры
//  ================================

#define SIMPLEFS_MAGIC 0x53465331
#define SIMPLEFS_SECTOR_SIZE 512

static char *disk_name = "/dev/loop0";
static unsigned long sb_first_sector = 0;
static unsigned long sb_second_sector = 101;
static unsigned int max_name_len = 32;
static unsigned int max_file_sectors = 4;

module_param(disk_name, charp, 0444);
module_param(sb_first_sector, ulong, 0444);
module_param(sb_second_sector, ulong, 0444);
module_param(max_name_len, uint, 0444);
module_param(max_file_sectors, uint, 0444);

struct simplefs_superblock
{
    u32 magic;
    u32 sector_size;
    u64 sb_first_sector;
    u64 sb_second_sector;
    u32 max_name_len;
    u32 max_file_sectors;
    u32 file_count;
    u32 hash;
};

struct simplefs_file
{
    char name[SIMPLEFS_MAX_NAME];
    u64 start_sector;
    u32 sectors;
};

static struct file *disk_file;
static struct simplefs_file *file_list;
static unsigned int file_count;
static u64 disk_sectors;

//               Хэш
//  ===============================

static u32 hash_update(u32 h, const void *data, size_t len)
{
    const u8 *p = data;
    size_t i;

    for (i = 0; i < len; i++)
    {
        h ^= p[i];
        h *= 16777619;
    }
    return h;
}

static u32 calc_hash(const void *data, size_t len)
{
    return hash_update(2166136261u, data, len);
}

static u32 calc_superblock_hash(struct simplefs_superblock *sb)
{
    struct simplefs_superblock tmp = *sb;

    tmp.hash = 0;
    return calc_hash(&tmp, sizeof(tmp));
}



//          Диск и секторы
//  ================================

static int open_disk(void)
{
    disk_file = bdev_file_open_by_path(disk_name,BLK_OPEN_READ | BLK_OPEN_WRITE, NULL, NULL);

    if (IS_ERR(disk_file)) return PTR_ERR(disk_file);

    pr_info("SimpleFS: disk otkrit: %s\n", disk_name);

    return 0;
}

static void close_disk(void)
{
    if (disk_file && !IS_ERR(disk_file)) fput(disk_file);
}

static int write_sector(u64 sektor, void *buf, size_t len)
{
    loff_t pos = sektor * SIMPLEFS_SECTOR_SIZE;

    if (kernel_write(disk_file, buf, len, &pos) != len) return -EIO;

    return 0;
}

static int read_sector(u64 sektor, void *buf, size_t len)
{
    loff_t pos = sektor * SIMPLEFS_SECTOR_SIZE;

    if (kernel_read(disk_file, buf, len, &pos) != len) return -EIO;

    return 0;
}


static int sector_is_empty(u64 sektor)
{
    char *buf;
    int i;

    buf = kzalloc(SIMPLEFS_SECTOR_SIZE, GFP_KERNEL);
    if (!buf) return 0;

    if (read_sector(sektor, buf, SIMPLEFS_SECTOR_SIZE))
    {
        kfree(buf);
        return 0;
    }

    for (i = 0; i < SIMPLEFS_SECTOR_SIZE; i++)
    {
        if (buf[i] != 0)
        {
            kfree(buf);
            return 0;
        }
    }

    kfree(buf);
    return 1;
}


//     Суперблоки и обычные файлы
//  ================================

static int file_place_is_free(u64 start)
{
    u32 i;
    for (i = 0; i < max_file_sectors; i++)
    {
        if ((start + i) == sb_first_sector || (start + i)  == sb_second_sector) return 0;
    }
    return 1;
}

static int build_file_list(void)
{
    u64 sektor = 0;
    unsigned int i = 0;

    disk_sectors = bdev_nr_bytes(file_bdev(disk_file)) / SIMPLEFS_SECTOR_SIZE; // Узнаём кол-во секторов, разделив доступное кол-во байт на размер сектора

    while (sektor + max_file_sectors <= disk_sectors)
    {
        if (file_place_is_free(sektor))
        {
            file_count++;
            sektor += max_file_sectors;
        }
        else
        {
            sektor++;
        }
    }

    file_list = kcalloc(file_count, sizeof(struct simplefs_file), GFP_KERNEL);
    if (!file_list) return -ENOMEM;

    sektor = 0;

    while (i < file_count && sektor + max_file_sectors <= disk_sectors)
    {
        if (!file_place_is_free(sektor))
        {
            sektor++;
            continue;
        }

        snprintf(file_list[i].name, SIMPLEFS_MAX_NAME, "file%u", i);
        file_list[i].start_sector = sektor;
        file_list[i].sectors = max_file_sectors;

        i++;
        sektor += max_file_sectors;
    }

    pr_info("SimpleFS: sektorov=%llu, file=%u\n", disk_sectors, file_count);

    return 0;
}

static void free_file_list(void)
{
    kfree(file_list);
}

static int find_file_by_name(const char *name, int len)
{
    unsigned int i;

    for (i = 0; i < file_count; i++)
    {
        if (strlen(file_list[i].name) == len && memcmp(file_list[i].name, name, len) == 0) return i;
    }

    return -1;
}

//    Взаимодействие с суперблоками
//  =================================

static int save_superblock(void)
{
    struct simplefs_superblock sb;

    memset(&sb, 0, sizeof(sb));

    sb.magic = SIMPLEFS_MAGIC;
    sb.sector_size = SIMPLEFS_SECTOR_SIZE;
    sb.sb_first_sector = sb_first_sector;
    sb.sb_second_sector = sb_second_sector;
    sb.max_name_len = max_name_len;
    sb.max_file_sectors = max_file_sectors;
    sb.file_count = file_count;
    sb.hash = calc_superblock_hash(&sb);

    write_sector(sb_first_sector, &sb, sizeof(sb));
    write_sector(sb_second_sector, &sb, sizeof(sb));

    pr_info("SimpleFS: superblock zapisan, hash=%u\n", sb.hash);

    return 0;
}

static int check_superblock(u64 sektor)
{
    struct simplefs_superblock sb;

    memset(&sb, 0, sizeof(sb));
    read_sector(sektor, &sb, sizeof(sb));

    if (sb.magic != SIMPLEFS_MAGIC) return -EINVAL;

    if (sb.hash != calc_superblock_hash(&sb)) return -EINVAL;

    return 0;
}

//          Read и write файлов
//  =================================

static ssize_t simplefs_read(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
    struct simplefs_file *info = file_inode(file)->i_private;
    char *tmp;
    loff_t pos;
    size_t file_size;

    file_size = info->sectors * SIMPLEFS_SECTOR_SIZE;

    if (*ppos >= file_size) return 0;

    if (len > file_size - *ppos) len = file_size - *ppos;

    tmp = kzalloc(len, GFP_KERNEL);
    if (!tmp) return -ENOMEM;

    pos = info->start_sector * SIMPLEFS_SECTOR_SIZE + *ppos;

    kernel_read(disk_file, tmp, len, &pos);
    copy_to_user(buf, tmp, len);

    *ppos += len;

    kfree(tmp);

    return len;
}

static ssize_t simplefs_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
    struct simplefs_file *info = file_inode(file)->i_private;
    char *tmp;
    loff_t pos;
    size_t file_size;

    file_size = info->sectors * SIMPLEFS_SECTOR_SIZE;

    if (*ppos >= file_size) return -ENOSPC;

    if (len > file_size - *ppos) len = file_size - *ppos;

    tmp = kzalloc(len, GFP_KERNEL);
    if (!tmp) return -ENOMEM;

    copy_from_user(tmp, buf, len);

    pos = info->start_sector * SIMPLEFS_SECTOR_SIZE + *ppos;

    kernel_write(disk_file, tmp, len, &pos);

    *ppos += len;

    kfree(tmp);

    return len;
}



//                IOCTL
//  ================================

static int zero_all_files(void)
{
    char *zero;
    unsigned int i;
    u32 j;

    zero = kzalloc(SIMPLEFS_SECTOR_SIZE, GFP_KERNEL);
    if (!zero) return -ENOMEM;

    for (i = 0; i < file_count; i++)
    {
        for (j = 0; j < file_list[i].sectors; j++)
            write_sector(file_list[i].start_sector + j, zero, SIMPLEFS_SECTOR_SIZE);
    }

    kfree(zero);
    return 0;
}

static int erase_fs(void)
{
    char *zero;

    zero_all_files();

    zero = kzalloc(SIMPLEFS_SECTOR_SIZE, GFP_KERNEL);
    if (!zero) return -ENOMEM;

    write_sector(sb_first_sector, zero, SIMPLEFS_SECTOR_SIZE);
    write_sector(sb_second_sector, zero, SIMPLEFS_SECTOR_SIZE);

    kfree(zero);

    return 0;
}

static int calc_file_hash(unsigned int number, u32 *hash)
{
    char *buf;
    u32 j;
    u32 h = 2166136261u;

    buf = kzalloc(SIMPLEFS_SECTOR_SIZE, GFP_KERNEL);
    if (!buf) return -ENOMEM;

    for (j = 0; j < file_list[number].sectors; j++)
    {
        read_sector(file_list[number].start_sector + j, buf, SIMPLEFS_SECTOR_SIZE);
        h = hash_update(h, buf, SIMPLEFS_SECTOR_SIZE);
    }

    kfree(buf);

    *hash = h;
    return 0;
}

static int ioctl_hashes(void __user *arg)
{
    struct simplefs_hashes_user req;
    struct simplefs_hash_info info;
    char __user *user_items;
    unsigned int i;
    unsigned int n;

    copy_from_user(&req, arg, sizeof(req));

    req.real_count = file_count;
    n = req.max_count;

    if (n > file_count) n = file_count;

    user_items = (char __user *)(unsigned long)req.user_addr;

    for (i = 0; i < n; i++)
    {
        memset(&info, 0, sizeof(info));

        strscpy(info.name, file_list[i].name, SIMPLEFS_MAX_NAME);
        info.start_sector = file_list[i].start_sector;
        info.sectors = file_list[i].sectors;
        calc_file_hash(i, &info.hash);

        copy_to_user(user_items + i * sizeof(info), &info, sizeof(info));
    }

    copy_to_user(arg, &req, sizeof(req));
    return 0;
}

static int ioctl_mapping(void __user *arg)
{
    struct simplefs_mapping_user req;
    int number;

    copy_from_user(&req, arg, sizeof(req));

    req.name[SIMPLEFS_MAX_NAME - 1] = '\0';

    number = find_file_by_name(req.name, strnlen(req.name, SIMPLEFS_MAX_NAME));
    if (number < 0) return -ENOENT;

    req.start_sector = file_list[number].start_sector;
    req.sectors = file_list[number].sectors;

    copy_to_user(arg, &req, sizeof(req));

    return 0;
}

static long simplefs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    switch (cmd)
    {
        case SIMPLEFS_IOCTL_ZERO_ALL:
            return zero_all_files();

        case SIMPLEFS_IOCTL_ERASE_FS:
            return erase_fs();

        case SIMPLEFS_IOCTL_GET_HASHES:
            return ioctl_hashes((void __user *)arg);

        case SIMPLEFS_IOCTL_GET_MAPPING:
            return ioctl_mapping((void __user *)arg);
    }

    return -ENOTTY;
}



//         Взаимодействие с VFS
//  =================================

static const struct file_operations simplefs_file_ops = {
    .owner = THIS_MODULE,
    .read = simplefs_read,
    .write = simplefs_write,
    .unlocked_ioctl = simplefs_ioctl,
    .llseek = default_llseek,
};

static struct inode *create_inode(struct super_block *sb, umode_t mode, unsigned long nomer_inode, struct simplefs_file *file_info)
{
    struct inode *inode;

    inode = new_inode(sb);
    if (!inode) return NULL;

    inode->i_ino = nomer_inode;
    inode_init_owner(&nop_mnt_idmap, inode, NULL, mode);
    inode->i_private = file_info;

    if (S_ISDIR(mode))
    {
        inode->i_size = SIMPLEFS_SECTOR_SIZE;
        set_nlink(inode, 2);
    }
    else
    {
        inode->i_size = max_file_sectors * SIMPLEFS_SECTOR_SIZE;
        set_nlink(inode, 1);
        inode->i_fop = &simplefs_file_ops;
    }

    return inode;
}

static int simplefs_readdir(struct file *file, struct dir_context *ctx)
{
    unsigned int i;

    if (ctx->pos < 2 && !dir_emit_dots(file, ctx)) return 0;

    for (i = ctx->pos - 2; i < file_count; i++)
    {
        if (!dir_emit(ctx, file_list[i].name, strlen(file_list[i].name), i + 2, DT_REG)) return 0;

        ctx->pos++;
    }

    return 0;
}

static struct dentry *simplefs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
    int number;
    struct inode *inode = NULL;

    number = find_file_by_name(dentry->d_name.name, dentry->d_name.len);

    if (number >= 0) inode = create_inode(dir->i_sb, S_IFREG | 0666, number + 2, &file_list[number]);

    d_add(dentry, inode);

    return NULL;
}

static const struct file_operations simplefs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = simplefs_readdir,
};

static const struct inode_operations simplefs_dir_inode_ops = {
    .lookup = simplefs_lookup,
};

static const struct super_operations simplefs_super_ops = {
    .statfs = simple_statfs,
    .drop_inode = generic_delete_inode,
};

static int simplefs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct inode *root_inode;

    sb->s_magic = SIMPLEFS_MAGIC;
    sb->s_op = &simplefs_super_ops;

    if (check_superblock(sb_first_sector) || check_superblock(sb_second_sector))
    {
        pr_err("SimpleFS: superblock povrezhden, mount zapreshen\n");
        return -EINVAL;
    }

    root_inode = create_inode(sb, S_IFDIR | 0755, 1, NULL);
    if (!root_inode) return -ENOMEM;

    root_inode->i_op = &simplefs_dir_inode_ops;
    root_inode->i_fop = &simplefs_dir_ops;

    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root) return -ENOMEM;

    return 0;
}

static struct dentry *simplefs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    return mount_bdev(fs_type, flags, dev_name, data, simplefs_fill_super);
}

static struct file_system_type simplefs_type = {
    .owner = THIS_MODULE,
    .name = "simplefs",
    .mount = simplefs_mount,
    .kill_sb = kill_block_super,
    .fs_flags = FS_REQUIRES_DEV,
};


//        Начало работы и конец
//  =================================

static int __init simplefs_init(void)
{
    int error;

    pr_info("SimpleFS: module loaded\n");

    if (max_file_sectors == 0 || sb_first_sector == sb_second_sector) return -EINVAL;

    error = open_disk();
    if (error) return error;

    error = build_file_list();
    if (error) {
        close_disk();
        return error;
    }

    if (sector_is_empty(sb_first_sector) && sector_is_empty(sb_second_sector))
        save_superblock();

    error = register_filesystem(&simplefs_type);
    if (error) {
        free_file_list();
        close_disk();
        return error;
    }

    pr_info("SimpleFS: filesystem registered\n");

    return 0;
}

static void __exit simplefs_exit(void)
{
    unregister_filesystem(&simplefs_type);
    free_file_list();
    close_disk();

    pr_info("SimpleFS: module unloaded\n");
}

module_init(simplefs_init);
module_exit(simplefs_exit);

MODULE_LICENSE("GPL");