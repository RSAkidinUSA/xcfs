#include "xcfs.h"

static int xcfs_create(struct inode* i, struct dentry* d, 
				umode_t u, bool b)
{
	return 0;
}

static int xcfs_mkdir(struct inode* i, struct dentry* d, umode_t u)
{
	return 0;
}

const struct inode_operations xcfs_inode_file_ops = {
    .create = xcfs_create,
};

const struct inode_operations xcfs_inode_dir_ops = {
	.create = xcfs_create,
	.mkdir = xcfs_mkdir,
	.lookup = xcfs_lookup,
};


