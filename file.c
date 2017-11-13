#include "xcfs.h"

/* encrypt and decrypt functions */
static void xcfs_decrypt(char* buf, size_t count)
{
	int i = 0;
	
	for(i = 0; i < count; ++i)
	{
		buf[i]--;
	}
}

static void xcfs_encrypt(char* buf, size_t count)
{
	int i = 0;
	for(i = 0; i < count; ++i)
	{
		buf[i]++;
	}
}

/* file operations in order of listing in struct */
/* llseek */
static loff_t xcfs_llseek(struct file* file, loff_t offset, int whence)
{
	int err;
	struct file *lower_file;

	err = generic_file_llseek(file, offset, whence);
	if(err < 0) {
		printk("xcfs_llseek: failed initial seek, aborting\n");
		return err;
	}
	
	lower_file = xcfs_lower_file(file);
	err = generic_file_llseek(lower_file, offset, whence);
	return err;
}

/* read */
static ssize_t xcfs_read(struct file *file, char __user *ubuf, size_t count, 
				loff_t *ppos)
{
	struct file *lower_file;
	char* buf = NULL;
	long retval = 0;
    int err = 0;
	struct dentry *dentry = file->f_path.dentry;

	lower_file = xcfs_lower_file(file);
	retval = vfs_read(lower_file, ubuf, count, ppos);
	if(retval >= 0)
	{
		fsstack_copy_attr_atime(dentry->d_inode, 
					file_inode(lower_file));
	}
	err = retval;

    /* decryption */
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
		printk("xcfs_read: failed to copy %ld from user\n", retval);
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

/* write */
static ssize_t xcfs_write(struct file *file, const char __user *ubuf, 
				size_t count, loff_t *ppos)
{
	long retval = 0;
	struct file *lower_file;
	char *buf = NULL;
	struct dentry *dentry = file->f_path.dentry;
        
    /* encryption */
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

	lower_file = xcfs_lower_file(file);
	retval = vfs_write(lower_file, ubuf, count, ppos);
	if(retval >= 0)
	{
		fsstack_copy_inode_size(dentry->d_inode,
					file_inode(lower_file));
		fsstack_copy_attr_times(dentry->d_inode,
					file_inode(lower_file));
	}

    /* encryption cont */
    /*
	xcfs_write_cleanup:
    kfree(buf);
    */

	return retval;
}

/* read iter */ 
static ssize_t xcfs_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	int err = 0;
	struct file *file = iocb->ki_filp, *lower_file;

	lower_file = xcfs_lower_file(file);
	if(!lower_file->f_op->read_iter)
	{
		return -EINVAL;
	}

	get_file(lower_file); /* prevent lower_file from being released */
	iocb->ki_filp = lower_file;
	err = lower_file->f_op->read_iter(iocb, iter);
	iocb->ki_filp = file;
	fput(lower_file);
	
	if(err >= 0 || err == -EIOCBQUEUED)
	{
		fsstack_copy_attr_atime(file->f_path.dentry->d_inode,
					file_inode(lower_file));
	}
	return err;
}

/* write iter */
static ssize_t xcfs_write_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	int err = 0;
	struct file *file = iocb->ki_filp, *lower_file;
	
	lower_file = xcfs_lower_file(file);
	if(!lower_file->f_op->write_iter)
	{
		return -EINVAL;
	}

	get_file(lower_file);
	iocb->ki_filp = lower_file;
	err = lower_file->f_op->write_iter(iocb, iter);
	iocb->ki_filp = file;
	fput(lower_file);

	if(err >= 0 || err == -EIOCBQUEUED)
	{
		fsstack_copy_inode_size(file->f_path.dentry->d_inode,
					file_inode(lower_file));
		fsstack_copy_attr_times(file->f_path.dentry->d_inode,
					file_inode(lower_file));
	}
	
	return err;
}	

/* iterate */
static int xcfs_iterate(struct file *file, struct dir_context *ctx)
{
	int err = 0;
	struct file *lower_file = NULL;
	struct dentry *dentry = file->f_path.dentry;

	lower_file = xcfs_lower_file(file);
	err = iterate_dir(lower_file, ctx);
	file->f_pos = lower_file->f_pos;
	if(err >= 0)
	{
		fsstack_copy_attr_atime(dentry->d_inode,
					file_inode(lower_file));
	}
	return err;
}



/* unlocked ioctl */
/* compat ioctl */
/* mmap */

/* open */
static int xcfs_open(struct inode *inode, struct file *file)
{
	int err = 0;
	struct file *lower_file = NULL;
	struct path lower_path;

	/*don't open unhashed/deleted files*/
	if(d_unhashed(file->f_path.dentry))
		return -ENOENT;

	
	file->private_data = 
		kzalloc(sizeof(struct xcfs_file_info), GFP_KERNEL);
	
	if(!XCFS_F(file))
		return -ENOMEM;
	

	xcfs_get_lower_path(file->f_path.dentry, &lower_path);
	lower_file = dentry_open(&lower_path, file->f_flags, current_cred());
	path_put(&lower_path);
	if(IS_ERR(lower_file))
	{
		err = PTR_ERR(lower_file);
		lower_file = xcfs_lower_file(file);
		if(lower_file)
		{
			xcfs_set_lower_file(file, NULL);
			fput(lower_file);
		}
	}
	else
	{
		xcfs_set_lower_file(file, lower_file);
	}

	if(err)
	{
		kfree(XCFS_F(file));
	}
	else
	{
		fsstack_copy_attr_all(inode, xcfs_lower_inode(inode));
	}
	
	return err;
}

/* flush */
static int xcfs_flush(struct file *file, fl_owner_t id)
{
	int err = 0;
	struct file *lower_file = NULL;

	lower_file = xcfs_lower_file(file);
	if(lower_file && lower_file->f_op && lower_file->f_op->flush)
	{
		filemap_write_and_wait(file->f_mapping);
		err = lower_file->f_op->flush(lower_file, id);
	}

	return err;
}

/* release */
static int xcfs_release(struct inode *inode, struct file *file)
{
	struct file *lower_file = NULL;

	lower_file = xcfs_lower_file(file);
	if(lower_file)
	{
		xcfs_set_lower_file(file, NULL);
		fput(lower_file);
	}	

	//kfree(WRAPFS_F(file);
	return 0;
}

/* fsync */
static int xcfs_fsync(struct file *file, loff_t start, loff_t end,
			int datasync)
{
	int err;
	struct file *lower_file;
	struct path lower_path;
	struct dentry *dentry = file->f_path.dentry;

	err = __generic_file_fsync(file, start, end, datasync);
	if (err)
		goto out;
	lower_file = xcfs_lower_file(file);
	xcfs_get_lower_path(dentry, &lower_path);
	err = vfs_fsync_range(lower_file, start, end, datasync);
	xcfs_put_lower_path(dentry, &lower_path);
out:
	return err;
}

/* fasync */
static int xcfs_fasync(int fd, struct file *file, int flag)
{
	int err = 0;
	struct file *lower_file = NULL;

	lower_file = xcfs_lower_file(file);
	if (lower_file->f_op && lower_file->f_op->fasync)
		err = lower_file->f_op->fasync(fd, lower_file, flag);

	return err;
}

/* lock */

/* file operations for files and dirs */
const struct file_operations xcfs_file_ops = {
	.llseek 	= xcfs_llseek,
	.read 		= xcfs_read,
	.write 		= xcfs_write,
	.read_iter 	= xcfs_read_iter,
	.write_iter = xcfs_write_iter,
	.iterate	= xcfs_iterate,
//	.unlocked_ioctl	= xcfs_unlocked_ioctl,
//	.compat_ioctl	= xcfs_compat_ioctl,
//	.mmap		= xcfs_mmap,
	.open		= xcfs_open,
	.flush		= xcfs_flush,
	.release	= xcfs_release,
	.fsync		= xcfs_fsync,
	.fasync		= xcfs_fasync,
//	.lock		= xcfs_lock,
};

const struct file_operations xcfs_dir_ops = {
	.llseek		= xcfs_llseek,
	.read		= generic_read_dir,
	.iterate	= xcfs_iterate,
//	.unlocked_ioctl	= xcfs_unlocked_ioctl,
//	.compat_ioctl	= xcfs_compat_ioctl,
	.open		= xcfs_open,
	.release	= xcfs_release,
	.fsync		= xcfs_fsync,
	.fasync		= xcfs_fasync,
//	.lock		= xcfs_lock,
};

