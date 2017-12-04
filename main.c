#include "xcfs.h"

/*
 * There is no need to lock the xcfs_super_info's rwsem as there is no
 * way anyone can have a reference to the superblock at this point in time.
 */
static int xcfs_read_super(struct super_block *sb, void *raw_data, int silent)
{
	int err = 0;
	struct super_block *lower_sb;
	struct path lower_path;
	char *dev_name = (char *) raw_data;
	struct inode *inode;

	if (!dev_name) {
		printk(KERN_ERR
		       "xcfs: read_super: missing dev_name argument\n");
		err = -EINVAL;
		goto out;
	}

	/* parse lower path */
	err = kern_path(dev_name, LOOKUP_FOLLOW | LOOKUP_DIRECTORY,
			&lower_path);
	if (err) {
		printk(KERN_ERR	"xcfs: error accessing "
		       "lower directory '%s'\n", dev_name);
		goto out;
	}

	/* allocate superblock private data */
	sb->s_fs_info = kzalloc(sizeof(struct xcfs_sb_info), GFP_KERNEL);
	if (!XCFS_SB(sb)) {
		printk(KERN_CRIT "xcfs: read_super: out of memory\n");
		err = -ENOMEM;
		goto out_free;
	}

	/* set the lower superblock field of upper superblock */
	lower_sb = lower_path.dentry->d_sb;
	atomic_inc(&lower_sb->s_active);
	xcfs_set_lower_super(sb, lower_sb);

	/* inherit maxbytes from lower file system */
	sb->s_maxbytes = lower_sb->s_maxbytes;

	/*
	 * Our c/m/atime granularity is 1 ns because we may stack on file
	 * systems whose granularity is as good.
	 */
	sb->s_time_gran = 1;

	sb->s_op = &xcfs_sb_ops;
    sb->s_xattr = xcfs_xattr_handlers;

	sb->s_export_op = &xcfs_export_ops; /* adding NFS support */

	/* get a new inode and allocate our root dentry */
	inode = xcfs_iget(sb, d_inode(lower_path.dentry));
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_sput;
	}
	sb->s_root = d_make_root(inode);
	if (!sb->s_root) {
		err = -ENOMEM;
		goto out_iput;
	}
	d_set_d_op(sb->s_root, &xcfs_dent_ops);

	/* link the upper and lower dentries */
	sb->s_root->d_fsdata = NULL;
	err = new_dentry_private_data(sb->s_root);
	if (err)
		goto out_freeroot;

	/* if get here: cannot have error */

	/* set the lower dentries for s_root */
	xcfs_set_lower_path(sb->s_root, &lower_path);

	/*
	 * No need to call interpose because we already have a positive
	 * dentry, which was instantiated by d_make_root.  Just need to
	 * d_rehash it.
	 */
	d_rehash(sb->s_root);
	if (!silent)
		printk(KERN_INFO
		       "xcfs: mounted on top of %s type %s\n",
		       dev_name, lower_sb->s_type->name);
	goto out; /* all is well */

	/* no longer needed: free_dentry_private_data(sb->s_root); */
out_freeroot:
	dput(sb->s_root);
out_iput:
	iput(inode);
out_sput:
	/* drop refs we took earlier */
	atomic_dec(&lower_sb->s_active);
	kfree(XCFS_SB(sb));
	sb->s_fs_info = NULL;
out_free:
	path_put(&lower_path);

out:
	return err;
}

/*
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
*/

static struct dentry *xcfs_mount(struct file_system_type *fs_type, int flags,
					char const *dev_name, void *raw_data)
{
	void *lower_path_name = (void *) dev_name;

	return mount_nodev(fs_type, flags, lower_path_name,
			   xcfs_read_super);
}

static struct file_system_type xcfs_type = {
	.owner = THIS_MODULE,
	.name = XCFS_NAME,
	.mount = xcfs_mount,
	.kill_sb = generic_shutdown_super,
	.fs_flags = 0,
};
MODULE_ALIAS_FS(XCFS_NAME);



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
