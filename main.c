#include "xcfs.h"

static int xcfs_fill_sb(struct super_block *sb, void *data, int silent)
{
	struct inode *root = NULL;
	
	sb->s_magic = XCFS_MAGIC_NUMBER;
	sb->s_op = &xcfs_sb_ops;

	root = new_inode(sb);

	if(!root)
	{
		printk("inode allocation failed\n");
		return -ENOMEM;
	}

	root->i_ino = 0;
	root->i_sb = sb;
	//root->i_atime = root->i_mtime = root->i_ctime = CURRENT_TIME;
	inode_init_owner(root, NULL, S_IFDIR);

	sb->s_root = d_make_root(root);
	if(!sb->s_root)
	{
		printk("root creation failed\n");
		//free root inode?
		return -ENOMEM;
	}

	return 0;
}

static struct dentry *xcfs_mount(struct file_system_type *type, int flags,
					char const *dev, void *data)
{
	struct dentry *const entry = mount_nodev(type, flags, data,
							xcfs_fill_sb);

	if(IS_ERR(entry)) {
		printk(PRINT_PREF "xcfs mounting failed\n");
    } else {
		printk(PRINT_PREF "xcfs mounted\n");
    }
	
	return entry;
}

static struct file_system_type xcfs_type = {
	.owner = THIS_MODULE,
	.name = XCFS_NAME,
	.mount = xcfs_mount,
	.kill_sb = generic_shutdown_super,
	.fs_flags = 0,
};



static int __init p4_init(void)
{
    int retval;
	
    printk(PRINT_PREF "Loading module: %s\n", XCFS_NAME);
    
    retval = xcfs_init_inode_cache();
    if (retval) {
        goto out;
    } 
    retval = xcfs_init_dentry_cache();
    if (retval) {
        goto out;
    }
	retval = register_filesystem(&xcfs_type);
out:
    if (retval) {
        xcfs_destroy_inode_cache();
        xcfs_destroy_dentry_cache();
    }
    return retval;
}

static void __exit p4_exit(void)
{
	printk(PRINT_PREF "Unloading module: %s\n", XCFS_NAME);
    xcfs_destroy_inode_cache();
    xcfs_destroy_dentry_cache();
	unregister_filesystem(&xcfs_type);
}

module_init(p4_init);
module_exit(p4_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Monger <mpm8128@vt.edu>, Ryan Burrow <rsardb11@vt.edu>");
MODULE_DESCRIPTION("XCFS - encrypted filesystem");
