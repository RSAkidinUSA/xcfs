#include "xcfs.h"

/* copied from wrapfs, this code checks if a dentry is valid */
/*
 * returns: -ERRNO if error (returned to user)
 *          0: tell VFS to invalidate dentry
 *          1: dentry is valid
 */
static int xcfs_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct path lower_path;
	struct dentry *lower_dentry;
	int err = 1;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	xcfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	if (!(lower_dentry->d_flags & DCACHE_OP_REVALIDATE))
		goto out;
	err = lower_dentry->d_op->d_revalidate(lower_dentry, flags);
out:
	xcfs_put_lower_path(dentry, &lower_path);
	return err;
}

/* copied from wrapfs, this code releases a locked dentry */
static void xcfs_d_release(struct dentry *dentry)
{
	/* release and reset the lower paths */
	xcfs_put_reset_lower_path(dentry);
	free_dentry_private_data(dentry);
	return;
}

/* copied from wrapfs, these are the operations for dentries */
const struct dentry_operations xcfs_dent_ops = {
	.d_revalidate	= xcfs_d_revalidate,
	.d_release	= xcfs_d_release,
};
