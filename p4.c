#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/fs_stack.h>

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

	if(IS_ERR(entry))
		printk("xcfs mounting failed\n");
	else
		printk("xcfs mounted\n");

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
	.fs_flags = 0,
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

static ssize_t xcfs_read(struct file *file, char __user *ubuf, size_t count, 
				loff_t *ppos)
{
	struct file *lower_file;
	//char* buf = NULL;
	long retval = 0;
	int err = 0;
	struct dentry *dentry = file->f_path.dentry;

	//lower_file = wrapfs_lower_file(file);
	retval = vfs_read(lower_file, ubuf, count, ppos);
	if(retval >= 0)
	{
		fsstack_copy_attr_atime(dentry->d_inode, 
					file_inode(lower_file));
	}
	err = retval;

	/*
	buf = kcalloc(count, sizeof(char), GFP_KERNEL);
	if(buf == NULL)
	{
		printk("xcfs_read: allocation failed for buf\n");
		return -1;
	}
	retval = copy_from_user(buf, ubuf, count);
	if(retval)
	{
		printk("xcfs_read: failed to copy %ld from user\n, retval");
		retval = -1;
		goto xcfs_read_cleanup;
	}
	
	xcfs_decrypt(buf, count);

	retval = copy_to_user(ubuf, buf, count);
	if(retval)
	{
		printk("xcfs_read: failed to copy %ld to user\n", retval);
		retval = -1;
		goto xcfs_read_cleanup;
	}
	retval = err;

	xcfs_read_cleanup:
	kfree(buf);	

	*/

	return retval;
}

static ssize_t xcfs_write(struct file *file, const char __user *ubuf, 
				size_t count, loff_t *ppos)
{
	long retval = 0;
	struct file *lower_file;
	//char *buf = NULL;
	struct dentry *dentry = file->f_path.dentry;

	/*
	buf = kcalloc(count, sizeof(char), GFP_KERNEL);
	if(buf == NULL)
	{
		printk("xcfs_write: allocation failed for buf\n");
		return -1;
	}
	
	retval = copy_from_user(buf, ubuf, count);
	if(retval)
	{
		printk("xcfs_write: failed to copy %ld from user\n", retval);
		retval = -1;
		goto xcfs_write_cleanup;
	}
	
	xcfs_encrypt(buf, count);

	retval = copy_to_user(ubuf, buf, count);
	if(retval)
	{
		printk("xcfs_write: failed to copy %ld to user\n", retval);
		retval = -1;
		goto xcfs_write_cleanup;
	}
	*/

	//lower_file = wrapfs_lower_file(file);
	retval = vfs_write(lower_file, ubuf, count, ppos);
	if(retval >= 0)
	{
		fsstack_copy_inode_size(dentry->d_inode,
					file_inode(lower_file));
		fsstack_copy_attr_times(dentry->d_inode,
					file_inode(lower_file));
	}

	/*
	xcfs_write_cleanup:
	kfree(buf);
	*/

	return retval;
}

static const struct file_operations xcfs_file_operations = {
	.read = xcfs_read,
	.write = xcfs_write,
};


static int __init p4_init(void)
{
	printk("Loading module: p4\n");
	return register_filesystem(&xcfs_type);
}

static void __exit p4_exit(void)
{
	printk("Unloading module p4\n");
	unregister_filesystem(&xcfs_type);
}

module_init(p4_init);
module_exit(p4_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Monger <mpm8128@vt.edu>, Ryan Burrow <rsardb11@vt.edu>");
MODULE_DESCRIPTION("XCFS - encrypted filesystem");
