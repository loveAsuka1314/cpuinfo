#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/path.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <asm/paravirt.h> // 包含 cr0 操作函数

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Developer");
MODULE_DESCRIPTION("Direct VFS Inode Replacement");
MODULE_VERSION("1.0");

static struct dentry *orig_cpuinfo_dentry = NULL;
static struct inode  *orig_cpuinfo_inode  = NULL;

// 存储你伪造的 cpuinfo 文本文件的绝对路径（请确保此文件在系统中真实存在且内容完整）
#define FAKE_CPUINFO_PATH "/data/local/tmp/cpuinfo.txt" 

// 临时关闭控制寄存器 CR0 的写保护（WP位）
static inline unsigned long disable_wp(void) {
    unsigned long cr0 = read_cr0();
    write_cr0(cr0 & ~0x00010000);
    return cr0;
}

// 恢复控制寄存器 CR0 的写保护
static inline void enable_wp(unsigned long cr0) {
    write_cr0(cr0);
}

static int __init vfs_replace_init(void) {
    struct path proc_path;
    struct path fake_path;
    unsigned long orig_cr0;
    int err;

    // 1. 获取 真实 /proc/cpuinfo 的路径结构
    err = kern_path("/proc/cpuinfo", LOOKUP_FOLLOW, &proc_path);
    if (err) {
        pr_err("[-] Cannot find /proc/cpuinfo\n");
        return err;
    }

    // 2. 获取 假文件的路径结构
    err = kern_path(FAKE_CPUINFO_PATH, LOOKUP_FOLLOW, &fake_path);
    if (err) {
        pr_err("[-] Cannot find fake file at %s, please create it first!\n", FAKE_CPUINFO_PATH);
        path_put(&proc_path);
        return err;
    }

    // 3. 备份原始的 /proc/cpuinfo 节点信息
    orig_cpuinfo_dentry = proc_path.dentry;
    orig_cpuinfo_inode  = (struct inode *)proc_path.dentry->d_inode;

    pr_info("[+] Swapping inode pointers in VFS layer...\n");
    
    // 4. 关闭写保护，强行修改内核只读指针
    orig_cr0 = disable_wp();
    
    // 执行指针替换：让 /proc/cpuinfo 的 dentry 指向假文件的 inode
    *(struct inode **)&(proc_path.dentry->d_inode) = (struct inode *)fake_path.dentry->d_inode;

    // 恢复写保护
    enable_wp(orig_cr0);

    // 5. 释放路径引用计数
    path_put(&proc_path);
    path_put(&fake_path);

    pr_info("[+] Successfully replaced /proc/cpuinfo with %s via Ko!\n", FAKE_CPUINFO_PATH);
    return 0;
}

static void __exit vfs_replace_exit(void) {
    unsigned long orig_cr0;
    
    // 卸载模块时必须还原，否则会导致严重的内核悬空指针崩溃（Kernel Panic）
    if (orig_cpuinfo_dentry && orig_cpuinfo_inode) {
        orig_cr0 = disable_wp();
        
        *(struct inode **)&(orig_cpuinfo_dentry->d_inode) = orig_cpuinfo_inode;
        
        enable_wp(orig_cr0);
        pr_info("[+] Restored original /proc/cpuinfo\n");
    }
}

module_init(vfs_replace_init);
module_exit(vfs_replace_exit);
