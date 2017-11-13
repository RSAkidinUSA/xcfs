#include "xcfs.h"

static void xcfs_destroy_inode(struct inode* node)
{

}

static void xcfs_put_super(struct super_block* sb)
{

}


const struct super_operations xcfs_sb_ops = {
	.destroy_inode = xcfs_destroy_inode,
	.put_super = xcfs_put_super,
};


