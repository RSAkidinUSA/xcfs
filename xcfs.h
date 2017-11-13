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

static void xcfs_destroy_inode(struct inode *inode);


#endif	/* not _XCFS_H_ */
