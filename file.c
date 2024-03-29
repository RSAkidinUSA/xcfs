#include "xcfs.h"

#include <linux/file.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/security.h>
#include <linux/compat.h>
#include <linux/fs_stack.h>


/* copied from wrapfs, and modified */
/* this function reads from a file, decrypts */
/* and writes into a buffer */
static ssize_t xcfs_read(struct file *file, char __user *ubuf, 
        size_t count, loff_t *ppos) 
{
	struct file *lower_file;
	char* buf = NULL;
	long retval = 0;
    	int err = 0;
	struct dentry *dentry = file->f_path.dentry;

	printk("xcfs_read\n");

	//if(count > (file->f_inode->i_size - *ppos))
	//{
	//	count = file->f_inode->i_size - *ppos;
	//}

	lower_file = xcfs_lower_file(file);
	retval = vfs_read(lower_file, ubuf, count, ppos);
	if(retval >= 0)	{
		fsstack_copy_attr_atime(dentry->d_inode, 
					file_inode(lower_file));
	}
	err = retval;


 
   	/* decryption */
	buf = kcalloc(count, sizeof(char), GFP_KERNEL);
	if(buf == NULL) {
		printk("xcfs_read: allocation failed for buf\n");
		return -1;
	}

    	retval = copy_from_user(buf, ubuf, count);

	if(retval) {
		printk("xcfs_read: failed to copy %ld from user\n", retval);
		retval = -1;
		goto xcfs_read_cleanup;
	}
	
	xcfs_decrypt(buf, count);

	retval = copy_to_user(ubuf, buf, count);
	if(retval) {
		printk("xcfs_read: failed to copy %ld to user\n", retval);
		retval = -1;
		goto xcfs_read_cleanup;
	}
	retval = err; 

	printk("xcfs_read: retval = %ld\n", retval);
xcfs_read_cleanup:
	kfree(buf);

	return retval;
}

/* copied from wrapfs with modification */
/* this function reads from a buffer, encrypts */
/* and writes the encrypted data to a file */
static ssize_t xcfs_write(struct file *file, const char __user *ubuf, 
        size_t count, loff_t *ppos) 
{
	struct file *lower_file;
	long retval = 0;
	char *buf = NULL;
	struct dentry *dentry = file->f_path.dentry;
	mm_segment_t old_fs = get_fs();
	set_fs(KERNEL_DS);
        
    	/* encryption */
	buf = kcalloc(count, sizeof(char), GFP_KERNEL);
	if(buf == NULL) {
		printk("xcfs_write: allocation failed for buf\n");
		return -1;
	}
    
	retval = copy_from_user(buf, ubuf, count);

	if(retval) {
		printk("xcfs_write: failed to copy %ld from user\n", retval);
		retval = -1;
		goto xcfs_write_cleanup;
	}
	
	xcfs_encrypt(buf, count);

	lower_file = xcfs_lower_file(file);
	retval = vfs_write(lower_file, buf, count, ppos);
	if(retval >= 0) {
 	       	fsstack_copy_inode_size(dentry->d_inode,
					file_inode(lower_file));
		fsstack_copy_attr_times(dentry->d_inode,
					file_inode(lower_file));
	}
	
	printk("xcfs_write: retval: %ld\n", retval);

	xcfs_write_cleanup:
    	kfree(buf);
	set_fs(old_fs);

	return retval;
}

/* copied from wrapfs */
/* this function iterates through the files in a directory */
static int xcfs_readdir(struct file *file, struct dir_context *ctx) 
{
	int err;
	struct file *lower_file = NULL;
	struct dentry *dentry = file->f_path.dentry;

	lower_file = xcfs_lower_file(file);
	err = iterate_dir(lower_file, ctx);
	file->f_pos = lower_file->f_pos;
	if (err >= 0) {		/* copy the atime */
		fsstack_copy_attr_atime(d_inode(dentry),
					file_inode(lower_file));
    }
	return err;
}

/* copied from wrapfs */
/* this function checks if the io buffer for a file is unlocked */
/* unlocked ioctl */
static long xcfs_unlocked_ioctl(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	long err = -ENOTTY;
	struct file *lower_file;

	lower_file = xcfs_lower_file(file);

	/* XXX: use vfs_ioctl if/when VFS exports it */
	if (!lower_file || !lower_file->f_op) {
		goto out;
    }
	if (lower_file->f_op->unlocked_ioctl) {
		err = lower_file->f_op->unlocked_ioctl(lower_file, cmd, arg);
    }

	/* some ioctls can change inode attributes (EXT2_IOC_SETFLAGS) */
	if (!err) {
		fsstack_copy_attr_all(file_inode(file),
				      file_inode(lower_file));
    }
out:
	return err;
}

/* compat ioctl */
#ifdef CONFIG_COMPAT
static long xcfs_compat_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	long err = -ENOTTY;
	struct file *lower_file;

	lower_file = xcfs_lower_file(file);

	/* XXX: use vfs_ioctl if/when VFS exports it */
	if (!lower_file || !lower_file->f_op)
		goto out;
	if (lower_file->f_op->compat_ioctl)
		err = lower_file->f_op->compat_ioctl(lower_file, cmd, arg);

out:
	return err;
}
#endif

/* copied from wrapfs and modified */
/* this function defines the actions for a file when mmap is called */
/* mmap */
static int xcfs_mmap(struct file *file, struct vm_area_struct *vma)
{
    
	int err = 0;
	bool willwrite;
	struct file *lower_file;
	const struct vm_operations_struct *saved_vm_ops = NULL;
	
	printk("xcfs_mmap\n");

	/* this might be deferred to mmap's writepage */
	willwrite = ((vma->vm_flags | VM_SHARED | VM_WRITE) == vma->vm_flags);

	/*
	 * File systems which do not implement ->writepage may use
	 * generic_file_readonly_mmap as their ->mmap op.  If you call
	 * generic_file_readonly_mmap with VM_WRITE, you'd get an -EINVAL.
	 * But we cannot call the lower ->mmap op, so we can't tell that
	 * writeable mappings won't work.  Therefore, our only choice is to
	 * check if the lower file system supports the ->writepage, and if
	 * not, return EINVAL (the same error that
	 * generic_file_readonly_mmap returns in that case).
	 */
	lower_file = xcfs_lower_file(file);


	if (willwrite && !lower_file->f_mapping->a_ops->writepage) {
		err = -EINVAL;
		printk(KERN_ERR "xcfs: lower file system does not "
		       "support writeable mmap\n");
		goto out;
	}
    // temp handler
    err = generic_file_mmap(file, vma);
    goto out;
	/*
	 * find and save lower vm_ops.
	 *
	 * XXX: the VFS should have a cleaner way of finding the lower vm_ops
	 */
	if (!XCFS_F(file)->lower_vm_ops) {
		err = lower_file->f_op->mmap(lower_file, vma);
		if (err) {
			printk(KERN_ERR "xcfs: lower mmap failed %d\n", err);
			goto out;
		}
		saved_vm_ops = vma->vm_ops; /* save: came from lower ->mmap */
	}

	/*
	 * Next 3 lines are all I need from generic_file_mmap.  I definitely
	 * don't want its test for ->readpage which returns -ENOEXEC.
	 */
	file_accessed(file);
	vma->vm_ops = &xcfs_vm_ops;

	file->f_mapping->a_ops = &xcfs_addr_ops; /* set our aops */
	if (!XCFS_F(file)->lower_vm_ops) /* save for our ->fault */
		XCFS_F(file)->lower_vm_ops = saved_vm_ops;

out:
	return err;
}

/* coped from wrapfs */
/* this function handles how an inode is opened */
/* open */
static int xcfs_open(struct inode *inode, struct file *file)
{
	int err = 0;
	struct file *lower_file = NULL;
	struct path lower_path;

	/* don't open unhashed/deleted files */
	if (d_unhashed(file->f_path.dentry)) {
		err = -ENOENT;
		goto out_err;
	}

	file->private_data =
		kzalloc(sizeof(struct xcfs_file_info), GFP_KERNEL);
	if (!XCFS_F(file)) {
		err = -ENOMEM;
		goto out_err;
	}

	/* open lower object and link xcfs's file struct to lower's */
	xcfs_get_lower_path(file->f_path.dentry, &lower_path);
	lower_file = dentry_open(&lower_path, file->f_flags, current_cred());
	path_put(&lower_path);
	if (IS_ERR(lower_file)) {
		err = PTR_ERR(lower_file);
		lower_file = xcfs_lower_file(file);
		if (lower_file) {
			xcfs_set_lower_file(file, NULL);
			fput(lower_file); /* fput calls dput for lower_dentry */
		}
	} else {
		xcfs_set_lower_file(file, lower_file);
	}

	if (err)
		kfree(XCFS_F(file));
	else
		fsstack_copy_attr_all(inode, xcfs_lower_inode(inode));
out_err:
	return err;
}

/* copied from wrapfs */
/* this function defines how a file should be flushed */
/* flush */
static int xcfs_flush(struct file *file, fl_owner_t id)
{
	int err = 0;
	struct file *lower_file = NULL;

	lower_file = xcfs_lower_file(file);
	if(lower_file && lower_file->f_op && lower_file->f_op->flush) {
		filemap_write_and_wait(file->f_mapping);
		err = lower_file->f_op->flush(lower_file, id);
	}

	return err;
}

/* copied from wrapfs */
/* this function defines how a file should be released */
/* release */
static int xcfs_release(struct inode *inode, struct file *file)
{
	struct file *lower_file = NULL;

	lower_file = xcfs_lower_file(file);
	if(lower_file) {
		xcfs_set_lower_file(file, NULL);
		fput(lower_file);
	}	

	kfree(XCFS_F(file));
	return 0;
}

/* copied from wrapfs */
/* this function defines how a file should be synced */
/* fsync */
static int xcfs_fsync(struct file *file, loff_t start, loff_t end,
			int datasync)
{
	int err;
	struct file *lower_file;
	struct path lower_path;
	struct dentry *dentry = file->f_path.dentry;

	err = __generic_file_fsync(file, start, end, datasync);
	if (err) {
		goto out;
    }
	lower_file = xcfs_lower_file(file);
	xcfs_get_lower_path(dentry, &lower_path);
	err = vfs_fsync_range(lower_file, start, end, datasync);
	xcfs_put_lower_path(dentry, &lower_path);
out:
	return err;
}

/* copied from wrapfs */
/* just another file operation, has to do with async syncing */
/* fasync */
static int xcfs_fasync(int fd, struct file *file, int flag)
{
	int err = 0;
	struct file *lower_file = NULL;

	lower_file = xcfs_lower_file(file);
	if (lower_file->f_op && lower_file->f_op->fasync) {
		err = lower_file->f_op->fasync(fd, lower_file, flag);
    }

	return err;
}

/* copied from wrapfs */
/* defines behavior for seeking through a file */
/* llseek */
static loff_t xcfs_llseek(struct file* file, loff_t offset, int whence)
{
	struct file *lower_file;

	lower_file = xcfs_lower_file(file);
    
    return vfs_llseek(lower_file, offset, whence);
}

/* copied from wrapfs */
/* defines behavior for reading a interator */
/* read iter */ 
static ssize_t xcfs_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	int err;
	struct file *file = iocb->ki_filp, *lower_file;

	lower_file = xcfs_lower_file(file);
	if (!lower_file->f_op->read_iter) {
		err = -EINVAL;
		goto out;
	}

	get_file(lower_file); /* prevent lower_file from being released */
	iocb->ki_filp = lower_file;
	err = lower_file->f_op->read_iter(iocb, iter);
	iocb->ki_filp = file;
	fput(lower_file);
	/* update upper inode atime as needed */
	if (err >= 0 || err == -EIOCBQUEUED) {
		fsstack_copy_attr_atime(d_inode(file->f_path.dentry),
					file_inode(lower_file));
    }
out:
	return err;
}

/* copied from wrapfs */
/* defines a behavior for writing to an iterator */
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


/* file operations for files */
const struct file_operations xcfs_file_ops = {
	.llseek 	= generic_file_llseek,
	.read 		= xcfs_read,
	.write 		= xcfs_write,
	.mmap		= xcfs_mmap,
	.open		= xcfs_open,
	.flush		= xcfs_flush,
	.unlocked_ioctl	= xcfs_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= xcfs_compat_ioctl,
#endif
	.release	= xcfs_release,
	.fsync		= xcfs_fsync,
	.fasync		= xcfs_fasync,
	.read_iter 	= xcfs_read_iter,
	.write_iter = xcfs_write_iter,
};

/* file operations for directories */
const struct file_operations xcfs_dir_ops = {
	.llseek		= xcfs_llseek,
	.read		= generic_read_dir,
	.iterate	= xcfs_readdir,
	.unlocked_ioctl	= xcfs_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= xcfs_compat_ioctl,
#endif
	.open		= xcfs_open,
	.release	= xcfs_release,
	.fsync		= xcfs_fsync,
	.fasync		= xcfs_fasync,
};

