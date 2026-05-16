#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>

#define SIMPLEFS_MAGIC 0x53465331

static char *imya_diska = "/dev/loop0";
static unsigned long sb1_sektor = 0;
static unsigned long sb2_sektor = 100;
static unsigned int max_dlina_imeni = 32;
static unsigned int max_sektorov_file = 4;

module_param(imya_diska, charp, 0444);
MODULE_PARM_DESC(imya_diska, "Disk dlya SimpleFS");

module_param(sb1_sektor, ulong, 0444);
MODULE_PARM_DESC(sb1_sektor, "Sektor osnovnogo superblock");

module_param(sb2_sektor, ulong, 0444);
MODULE_PARM_DESC(sb2_sektor, "Sektor backup superblock");

module_param(max_dlina_imeni, uint, 0444);
MODULE_PARM_DESC(max_dlina_imeni, "Maksimalnaya dlina imeni file");

module_param(max_sektorov_file, uint, 0444);
MODULE_PARM_DESC(max_sektorov_file, "Maksimalniy razmer file v sektorah");

struct simplefs_superblock {
    u32 magic;
    u32 razmer_sektora;

    u64 sb1_sektor;
    u64 sb2_sektor;

    u32 max_dlina_imeni;
    u32 max_sektorov_file;

    u32 kolvo_file;
    u32 hash;
};

struct simplefs_file {
    char imya[64];
    u64 start_sektor;
    u32 razmer_v_sektorah;
};

static int __init simplefs_init(void)
{
    pr_info("SimpleFS: module loaded\n");

    pr_info("SimpleFS: imya_diska = %s\n", imya_diska);
    pr_info("SimpleFS: sb1_sektor = %lu\n", sb1_sektor);
    pr_info("SimpleFS: sb2_sektor = %lu\n", sb2_sektor);
    pr_info("SimpleFS: max_dlina_imeni = %u\n", max_dlina_imeni);
    pr_info("SimpleFS: max_sektorov_file = %u\n", max_sektorov_file);

    return 0;
}

static void __exit simplefs_exit(void)
{
    pr_info("SimpleFS: module unloaded\n");
}

module_init(simplefs_init);
module_exit(simplefs_exit);

MODULE_LICENSE("GPL");