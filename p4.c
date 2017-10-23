#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs.h>

#define XCFS_MAGIC_NUMBER 	0x69
#define CURRENT_TIME		1000

static void xcfs_destroy_inode(struct inode* node)
{

}

static void xcfs_put_super(struct super_block* sb)
{

}

static const struct super_operations xcfs_sb_ops = {
	.destroy_inode = xcfs_destroy_inode,
	.put_super = xcfs_put_super,
};

static int xcfs_fill_sb(struct super_block *sb, void *data, int silent)
{
	struct inode *root = NULL;
	
	sb->s_magic = XCFS_MAGIC_NUMBER;
	sb->s_op = &xcfs_sb_ops;

	root = new_inode(sb);

	if(!root)
	{
		pr_err("inode allocation failed\n");
		return -ENOMEM;
	}

	root->i_ino = 0;
	root->i_sb = sb;
	//root->i_atime = root->i_mtime = root->i_ctime = CURRENT_TIME;
	inode_init_owner(root, NULL, S_IFDIR);

	sb->s_root = d_make_root(root);
	if(!sb->s_root)
	{
		pr_err("root creation failed\n");
		//free root inode?
		return -ENOMEM;
	}

	return 0;
}

static struct dentry *xcfs_mount(struct file_system_type *type, int flags,
					char const *dev, void *data)
{
	struct dentry *const entry = mount_bdev(type, flags, dev, data,
							xcfs_fill_sb);

	if(IS_ERR(entry))
		pr_err("xcfs mounting failed\n");
	else
		pr_debug("xcfs mounted\n");

	return entry;
}

static void xcfs_kill_sb(struct super_block* sb)
{
	
}

static struct file_system_type xcfs_type = {
	.owner = THIS_MODULE,
	.name = "xcfs",
	.mount = xcfs_mount,
	.kill_sb = xcfs_kill_sb,
	.fs_flags = FS_REQUIRES_DEV,
};



static int xcfs_create(struct inode* i, struct dentry* d, 
				umode_t u, bool b)
{
	return 0;
}

static int xcfs_mkdir(struct inode* i, struct dentry* d, umode_t u)
{
	return 0;
}

static struct dentry* xcfs_lookup(struct inode* i, struct dentry* d, 
					unsigned int u)
{
	return NULL;
}

static const struct inode_operations xcfs_inode_ops = {
	.create = xcfs_create,
	.mkdir = xcfs_mkdir,
	.lookup = xcfs_lookup,
};
/*
static struct dirent* xcfs_readdir(DIR* dirp)
{
	return NULL;
}
*/
static ssize_t xcfs_read(struct file* f, char __user* ubuf, size_t st, 
				loff_t* lt)
{
	return 0;
}

static ssize_t xcfs_write(struct file* f, const char __user* ubuf, 
				size_t st, loff_t* lt)
{
	return 0;
}

static const struct file_operations xcfs_dir_operations = {
	.owner = THIS_MODULE,
//	.readdir = NULL;//xcfs_readdir,
};

static const struct file_operations xcfs_file_operations = {
	.read = xcfs_read,
	.write = xcfs_write,
};


static int __init p4_init(void)
{
	printk("Loading module: p4\n");
	return 0;
}

static void __exit p4_exit(void)
{
	printk("Unloading module p4\n");
}

module_init(p4_init);
module_exit(p4_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Monger <mpm8128@vt.edu>");
MODULE_DESCRIPTION("XCFS - encrypted filesystem");
