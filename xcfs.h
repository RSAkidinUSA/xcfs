#ifndef _XCFS_H_
#define _XCFS_H_

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/fs_stack.h>
#include <linux/file.h>

#define XCFS_MAGIC_NUMBER 	0x69
#define CURRENT_TIME		1000
#define XCFS_NAME           "xcfs"

/* operations vectors defined in specific files */
extern const struct inode_operations xcfs_inode_ops;
extern const struct super_operations xcfs_sb_ops;
extern const struct file_operations xcfs_file_ops;
extern const struct file_operations xcfs_dir_ops;

#endif	/* not _XCFS_H_ */
