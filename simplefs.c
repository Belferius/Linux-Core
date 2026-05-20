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

static char *imya_diska = "/dev/loop0";
static unsigned long sb1_sektor = 0;
static unsigned long sb2_sektor = 101;
static unsigned int max_dlina_imeni = 32;
static unsigned int max_sektorov_file = 4;

module_param(imya_diska, charp, 0444);
module_param(sb1_sektor, ulong, 0444);
module_param(sb2_sektor, ulong, 0444);
module_param(max_dlina_imeni, uint, 0444);
module_param(max_sektorov_file, uint, 0444);

struct simplefs_superblock
{
    u32 magic;
    u32 razmer_sektora;
    u64 sb1_sektor;
    u64 sb2_sektor;
    u32 max_dlina_imeni;
    u32 max_sektorov_file;
    u32 kolvo_file;
    u32 hash;
};

struct simplefs_file
{
    char imya[SIMPLEFS_MAX_NAME];
    u64 start_sektor;
    u32 razmer_v_sektorah;
};

static struct file *file_diska;
static struct simplefs_file *spisok_file;
static unsigned int kolvo_file;
static u64 kolvo_sektorov_diska;

//               Хэш
//  ===============================

static u32 hash_dobavit(u32 h, const void *data, size_t len)
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

static u32 schitat_hash(const void *data, size_t len)
{
    return hash_dobavit(2166136261u, data, len);
}

static u32 schitat_hash_sb(struct simplefs_superblock *sb)
{
    struct simplefs_superblock tmp = *sb;

    tmp.hash = 0;
    return schitat_hash(&tmp, sizeof(tmp));
}



//          Диск и секторы
//  ================================

static int otkrit_disk(void)
{
    file_diska = bdev_file_open_by_path(imya_diska,BLK_OPEN_READ | BLK_OPEN_WRITE, NULL, NULL);

    if (IS_ERR(file_diska)) return PTR_ERR(file_diska);

    pr_info("SimpleFS: disk otkrit: %s\n", imya_diska);

    return 0;
}

static void zakrit_disk(void)
{
    if (file_diska && !IS_ERR(file_diska)) fput(file_diska);
}

static int pisat_v_sektor(u64 sektor, void *buf, size_t len)
{
    loff_t pos = sektor * SIMPLEFS_SECTOR_SIZE;

    if (kernel_write(file_diska, buf, len, &pos) != len) return -EIO;

    return 0;
}

static int chitat_iz_sektora(u64 sektor, void *buf, size_t len)
{
    loff_t pos = sektor * SIMPLEFS_SECTOR_SIZE;

    if (kernel_read(file_diska, buf, len, &pos) != len) return -EIO;

    return 0;
}


//     Суперблоки и обычные файлы
//  ================================

static int mesto_pod_file_svobodno(u64 start)
{
    u32 i;
    for (i = 0; i < max_sektorov_file; i++)
    {
        if ((start + i) == sb1_sektor || (start + i)  == sb2_sektor) return 0;
    }
    return 1;
}

static int sozdat_spisok_file(void)
{
    u64 sektor = 0;
    unsigned int i = 0;

    kolvo_sektorov_diska = bdev_nr_bytes(file_bdev(file_diska)) / SIMPLEFS_SECTOR_SIZE; // Узнаём кол-во секторов, разделив доступное кол-во байт на размер сектора

    while (sektor + max_sektorov_file <= kolvo_sektorov_diska)
    {
        if (mesto_pod_file_svobodno(sektor))
        {
            kolvo_file++;
            sektor += max_sektorov_file;
        }
        else
        {
            sektor++;
        }
    }

    spisok_file = kcalloc(kolvo_file, sizeof(struct simplefs_file), GFP_KERNEL);
    if (!spisok_file) return -ENOMEM;

    sektor = 0;

    while (i < kolvo_file && sektor + max_sektorov_file <= kolvo_sektorov_diska)
    {
        if (!mesto_pod_file_svobodno(sektor))
        {
            sektor++;
            continue;
        }

        snprintf(spisok_file[i].imya, SIMPLEFS_MAX_NAME, "file%u", i);
        spisok_file[i].start_sektor = sektor;
        spisok_file[i].razmer_v_sektorah = max_sektorov_file;

        i++;
        sektor += max_sektorov_file;
    }

    pr_info("SimpleFS: sektorov=%llu, file=%u\n", kolvo_sektorov_diska, kolvo_file);

    return 0;
}

static void ochistit_spisok_file(void)
{
    kfree(spisok_file);
}

static int naiti_file_po_imeni(const char *imya, int dlina)
{
    unsigned int i;

    for (i = 0; i < kolvo_file; i++)
    {
        if (strlen(spisok_file[i].imya) == dlina && memcmp(spisok_file[i].imya, imya, dlina) == 0) return i;
    }

    return -1;
}

//    Взаимодействие с суперблоками
//  =================================

static int sohranit_superblock(void)
{
    struct simplefs_superblock sb;

    memset(&sb, 0, sizeof(sb));

    sb.magic = SIMPLEFS_MAGIC;
    sb.razmer_sektora = SIMPLEFS_SECTOR_SIZE;
    sb.sb1_sektor = sb1_sektor;
    sb.sb2_sektor = sb2_sektor;
    sb.max_dlina_imeni = max_dlina_imeni;
    sb.max_sektorov_file = max_sektorov_file;
    sb.kolvo_file = kolvo_file;
    sb.hash = schitat_hash_sb(&sb);

    pisat_v_sektor(sb1_sektor, &sb, sizeof(sb));
    pisat_v_sektor(sb2_sektor, &sb, sizeof(sb));

    pr_info("SimpleFS: superblock zapisan, hash=%u\n", sb.hash);

    return 0;
}

static int proverit_superblock(u64 sektor)
{
    struct simplefs_superblock sb;

    memset(&sb, 0, sizeof(sb));
    chitat_iz_sektora(sektor, &sb, sizeof(sb));

    if (sb.magic != SIMPLEFS_MAGIC) return -EINVAL;

    if (sb.hash != schitat_hash_sb(&sb)) return -EINVAL;

    return 0;
}

//          Read и write файлов
//  =================================

static ssize_t simplefs_read(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
    struct simplefs_file *info = file_inode(file)->i_private;
    char *tmp;
    loff_t pos;
    size_t razmer_file;

    razmer_file = info->razmer_v_sektorah * SIMPLEFS_SECTOR_SIZE;

    if (*ppos >= razmer_file) return 0;

    if (len > razmer_file - *ppos) len = razmer_file - *ppos;

    tmp = kzalloc(len, GFP_KERNEL);
    if (!tmp) return -ENOMEM;

    pos = info->start_sektor * SIMPLEFS_SECTOR_SIZE + *ppos;

    kernel_read(file_diska, tmp, len, &pos);
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
    size_t razmer_file;

    razmer_file = info->razmer_v_sektorah * SIMPLEFS_SECTOR_SIZE;

    if (*ppos >= razmer_file) return -ENOSPC;

    if (len > razmer_file - *ppos) len = razmer_file - *ppos;

    tmp = kzalloc(len, GFP_KERNEL);
    if (!tmp) return -ENOMEM;

    copy_from_user(tmp, buf, len);

    pos = info->start_sektor * SIMPLEFS_SECTOR_SIZE + *ppos;

    kernel_write(file_diska, tmp, len, &pos);

    *ppos += len;

    kfree(tmp);

    return len;
}



//                IOCTL
//  ================================

static int obnulit_vse_file(void)
{
    char *zero;
    unsigned int i;
    u32 j;

    zero = kzalloc(SIMPLEFS_SECTOR_SIZE, GFP_KERNEL);
    if (!zero) return -ENOMEM;

    for (i = 0; i < kolvo_file; i++)
    {
        for (j = 0; j < spisok_file[i].razmer_v_sektorah; j++)
            pisat_v_sektor(spisok_file[i].start_sektor + j, zero, SIMPLEFS_SECTOR_SIZE);
    }

    kfree(zero);
    return 0;
}

static int steret_fs(void)
{
    char *zero;

    obnulit_vse_file();

    zero = kzalloc(SIMPLEFS_SECTOR_SIZE, GFP_KERNEL);
    if (!zero) return -ENOMEM;

    pisat_v_sektor(sb1_sektor, zero, SIMPLEFS_SECTOR_SIZE);
    pisat_v_sektor(sb2_sektor, zero, SIMPLEFS_SECTOR_SIZE);

    kfree(zero);

    return 0;
}

static int poschitat_hash_file(unsigned int nomer, u32 *hash)
{
    char *buf;
    u32 j;
    u32 h = 2166136261u;

    buf = kzalloc(SIMPLEFS_SECTOR_SIZE, GFP_KERNEL);
    if (!buf) return -ENOMEM;

    for (j = 0; j < spisok_file[nomer].razmer_v_sektorah; j++)
    {
        chitat_iz_sektora(spisok_file[nomer].start_sektor + j, buf, SIMPLEFS_SECTOR_SIZE);
        h = hash_dobavit(h, buf, SIMPLEFS_SECTOR_SIZE);
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

    req.real_kolvo = kolvo_file;
    n = req.max_kolvo;

    if (n > kolvo_file) n = kolvo_file;

    user_items = (char __user *)(unsigned long)req.user_adres;

    for (i = 0; i < n; i++)
    {
        memset(&info, 0, sizeof(info));

        strscpy(info.imya, spisok_file[i].imya, SIMPLEFS_MAX_NAME);
        info.start_sektor = spisok_file[i].start_sektor;
        info.razmer_v_sektorah = spisok_file[i].razmer_v_sektorah;
        poschitat_hash_file(i, &info.hash);

        copy_to_user(user_items + i * sizeof(info), &info, sizeof(info));
    }

    copy_to_user(arg, &req, sizeof(req));
    return 0;
}

static int ioctl_mapping(void __user *arg)
{
    struct simplefs_mapping_user req;
    int nomer;

    copy_from_user(&req, arg, sizeof(req));

    req.imya[SIMPLEFS_MAX_NAME - 1] = '\0';

    nomer = naiti_file_po_imeni(req.imya, strnlen(req.imya, SIMPLEFS_MAX_NAME));
    if (nomer < 0) return -ENOENT;

    req.start_sektor = spisok_file[nomer].start_sektor;
    req.razmer_v_sektorah = spisok_file[nomer].razmer_v_sektorah;

    copy_to_user(arg, &req, sizeof(req));

    return 0;
}

static long simplefs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    switch (cmd)
    {
        case SIMPLEFS_IOCTL_ZERO_ALL:
            return obnulit_vse_file();

        case SIMPLEFS_IOCTL_ERASE_FS:
            return steret_fs();

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

static struct inode *sozdat_inode(struct super_block *sb, umode_t mode, unsigned long nomer_inode, struct simplefs_file *file_info)
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
        inode->i_size = max_sektorov_file * SIMPLEFS_SECTOR_SIZE;
        set_nlink(inode, 1);
        inode->i_fop = &simplefs_file_ops;
    }

    return inode;
}

static int simplefs_readdir(struct file *file, struct dir_context *ctx)
{
    unsigned int i;

    if (ctx->pos < 2 && !dir_emit_dots(file, ctx)) return 0;

    for (i = ctx->pos - 2; i < kolvo_file; i++)
    {
        if (!dir_emit(ctx, spisok_file[i].imya, strlen(spisok_file[i].imya), i + 2, DT_REG)) return 0;

        ctx->pos++;
    }

    return 0;
}

static struct dentry *simplefs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
    int nomer;
    struct inode *inode = NULL;

    nomer = naiti_file_po_imeni(dentry->d_name.name, dentry->d_name.len);

    if (nomer >= 0) inode = sozdat_inode(dir->i_sb, S_IFREG | 0666, nomer + 2, &spisok_file[nomer]);

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

    root_inode = sozdat_inode(sb, S_IFDIR | 0755, 1, NULL);
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
    int oshibka;

    pr_info("SimpleFS: module loaded\n");

    if (max_sektorov_file == 0 || sb1_sektor == sb2_sektor) return -EINVAL;

    oshibka = otkrit_disk();
    if (oshibka) return oshibka;

    oshibka = sozdat_spisok_file();
    if (oshibka) {
        zakrit_disk();
        return oshibka;
    }

    if (proverit_superblock(sb1_sektor) || proverit_superblock(sb2_sektor)) sohranit_superblock();

    if (proverit_superblock(sb1_sektor) || proverit_superblock(sb2_sektor))
    {
        ochistit_spisok_file();
        zakrit_disk();
        return -EINVAL;
    }

    oshibka = register_filesystem(&simplefs_type);
    if (oshibka) {
        ochistit_spisok_file();
        zakrit_disk();
        return oshibka;
    }

    pr_info("SimpleFS: filesystem registered\n");

    return 0;
}

static void __exit simplefs_exit(void)
{
    unregister_filesystem(&simplefs_type);
    ochistit_spisok_file();
    zakrit_disk();

    pr_info("SimpleFS: module unloaded\n");
}

module_init(simplefs_init);
module_exit(simplefs_exit);

MODULE_LICENSE("GPL");