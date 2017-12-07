#include "xcfs.h"

static struct kmem_cache *xcfs_inode_cachep;

/* copied from wrapfs */
/* this function defines how to fill a superblock */
static void xcfs_put_super(struct super_block* sb)
{
    struct xcfs_sb_info *spd;
	struct super_block *s;

	spd = XCFS_SB(sb);
	if (!spd)
		return;

	/* decrement lower super references */
	s = xcfs_lower_super(sb);
	xcfs_set_lower_super(sb, NULL);
	atomic_dec(&s->s_active);

	kfree(spd);
	sb->s_fs_info = NULL;
}

/* copied from wrapfs */
/* this function defines how to get information on a dentry in a superblock */
static int xcfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	int err;
	struct path lower_path;

	xcfs_get_lower_path(dentry, &lower_path);
	err = vfs_statfs(&lower_path, buf);
	xcfs_put_lower_path(dentry, &lower_path);

	/* set return buf to our f/s to avoid confusing user-level utils */
	buf->f_type = XCFS_MAGIC_NUMBER;

	return err;
}

/* copied from wrapfs */
/* this function defines how to remount a filesystem */
static int xcfs_remount_fs(struct super_block *sb, int *flags, char *options)
{
	int err = 0;

	/*
	 * The VFS will take care of "ro" and "rw" flags among others.  We
	 * can safely accept a few flags (RDONLY, MANDLOCK), and honor
	 * SILENT, but anything else left over is an error.
	 */
	if ((*flags & ~(MS_RDONLY | MS_MANDLOCK | MS_SILENT)) != 0) {
		printk(KERN_ERR
		       "xcfs: remount flags 0x%x unsupported\n", *flags);
		err = -EINVAL;
	}

	return err;
}

/*
 * Called by iput() when the inode reference count reached zero
 * and the inode is not hashed anywhere.  Used to clear anything
 * that needs to be, before the inode is completely destroyed and put
 * on the inode free list.
 */
/* copied from wrapfs */
/* this function defines how to evict an inode */
static void xcfs_evict_inode(struct inode *inode)
{
	struct inode *lower_inode;

	truncate_inode_pages(&inode->i_data, 0);
	clear_inode(inode);
	/*
	 * Decrement a reference to a lower_inode, which was incremented
	 * by our read_inode when it was created initially.
	 */
	lower_inode = xcfs_lower_inode(inode);
	xcfs_set_lower_inode(inode, NULL);
	iput(lower_inode);
}

/* copied from wrapfs */
/* this function defines how to allocate an inode */
static struct inode *xcfs_alloc_inode(struct super_block *sb)
{
	struct xcfs_inode_info *i;

	i = kmem_cache_alloc(xcfs_inode_cachep, GFP_KERNEL);
	if (!i)
		return NULL;

	/* memset everything up to the inode to 0 */
	memset(i, 0, offsetof(struct xcfs_inode_info, vfs_inode));

	i->vfs_inode.i_version = 1;
	return &i->vfs_inode;
}

/* copied from wrapfs */
/* this function defines how to destroy an inode */
static void xcfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(xcfs_inode_cachep, XCFS_I(inode));
}

/* xcfs inode cache constructor */
static void init_once(void *obj)
{
	struct xcfs_inode_info *i = obj;

	inode_init_once(&i->vfs_inode);
}

/* copied from wrapfs */
/* this function defines behaviour for initializing an inode cache */
int xcfs_init_inode_cache(void)
{
	int err = 0;

	xcfs_inode_cachep =
		kmem_cache_create("xcfs_inode_cache",
				  sizeof(struct xcfs_inode_info), 0,
				  SLAB_RECLAIM_ACCOUNT, init_once);
	if (!xcfs_inode_cachep)
		err = -ENOMEM;
	return err;
}

/* copied from wrapfs */
/* xcfs inode cache destructor */
void xcfs_destroy_inode_cache(void)
{
	if (xcfs_inode_cachep)
		kmem_cache_destroy(xcfs_inode_cachep);
}

/*
 * Used only in nfs, to kill any pending RPC tasks, so that subsequent
 * code can actually succeed and won't leave tasks that need handling.
 */
/* copied from wrapfs */
/* this function defines how to unmount a superblock */
static void xcfs_umount_begin(struct super_block *sb)
{
	struct super_block *lower_sb;

	lower_sb = xcfs_lower_super(sb);
	if (lower_sb && lower_sb->s_op && lower_sb->s_op->umount_begin)
		lower_sb->s_op->umount_begin(lower_sb);
}



const struct super_operations xcfs_sb_ops = {
	.put_super	    = xcfs_put_super,
	.statfs		    = xcfs_statfs,
	.remount_fs	    = xcfs_remount_fs,
	.evict_inode	= xcfs_evict_inode,
	.umount_begin	= xcfs_umount_begin,
	.show_options	= generic_show_options,
	.alloc_inode	= xcfs_alloc_inode,
	.destroy_inode	= xcfs_destroy_inode,
	.drop_inode	    = generic_delete_inode,
};

/* NFS support */

static struct inode *xcfs_nfs_get_inode(struct super_block *sb, u64 ino,
					  u32 generation)
{
	struct super_block *lower_sb;
	struct inode *inode;
	struct inode *lower_inode;

	lower_sb = xcfs_lower_super(sb);
	lower_inode = ilookup(lower_sb, ino);
	inode = xcfs_iget(sb, lower_inode);
	return inode;
}

static struct dentry *xcfs_fh_to_dentry(struct super_block *sb,
					  struct fid *fid, int fh_len,
					  int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    xcfs_nfs_get_inode);
}

static struct dentry *xcfs_fh_to_parent(struct super_block *sb,
					  struct fid *fid, int fh_len,
					  int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    xcfs_nfs_get_inode);
}

/*
 * all other funcs are default as defined in exportfs/expfs.c
 */

const struct export_operations xcfs_export_ops = {
	.fh_to_dentry	   = xcfs_fh_to_dentry,
	.fh_to_parent	   = xcfs_fh_to_parent
};
