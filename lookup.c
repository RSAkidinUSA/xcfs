#include "xcfs.h"
const char XCFS_SALT[] = "SALTIEST SALT OF THE SEA";

/* The dentry cache is just so we have properly sized dentries */
static struct kmem_cache *xcfs_dentry_cachep;

/* copied from wrapfs */
/* this function inits a dentry cache */
int xcfs_init_dentry_cache(void)
{
	xcfs_dentry_cachep =
		kmem_cache_create("xcfs_dentry",
				  sizeof(struct xcfs_dentry_info),
				  0, SLAB_RECLAIM_ACCOUNT, NULL);

	return xcfs_dentry_cachep ? 0 : -ENOMEM;
}

/* copied from wrapfs */
/* this function destoys a dentry cache */
void xcfs_destroy_dentry_cache(void)
{
	if (xcfs_dentry_cachep) {
		kmem_cache_destroy(xcfs_dentry_cachep);
    }
}

/* copied from wrapfs */
/* this function frees a dentry's private data */
void free_dentry_private_data(struct dentry *dentry)
{
	if (!dentry || !dentry->d_fsdata){
		return;
    }
	kmem_cache_free(xcfs_dentry_cachep, dentry->d_fsdata);
	dentry->d_fsdata = NULL;
}

/* copied from wrapfs */
/* allocate new dentry private data */
int new_dentry_private_data(struct dentry *dentry)
{
	struct xcfs_dentry_info *info = XCFS_D(dentry);

	/* use zalloc to init dentry_info.lower_path */
	info = kmem_cache_zalloc(xcfs_dentry_cachep, GFP_ATOMIC);
	if (!info) {
		return -ENOMEM;
    }

	spin_lock_init(&info->lock);
	dentry->d_fsdata = info;

	return 0;
}

/* copied from wrapfs */
/* this function tests if a lower inode is the lower inode of another inode */
static int xcfs_inode_test(struct inode *inode, void *candidate_lower_inode)
{
	struct inode *current_lower_inode = xcfs_lower_inode(inode);
	if (current_lower_inode == (struct inode *)candidate_lower_inode) {
		return 1; /* found a match */
    } else {
		return 0; /* no match */
    }
}

/* copied from wrapfs */
/* this function does nothing */
static int xcfs_inode_set(struct inode *inode, void *lower_inode)
{
	/* we do actual inode initialization in xcfs_iget */
	return 0;
}

/* copied from wrapfs */
/* this function gets an inode from a superblock */
struct inode *xcfs_iget(struct super_block *sb, struct inode *lower_inode)
{
	struct xcfs_inode_info *info;
	struct inode *inode; /* the new inode to return */

	if (!igrab(lower_inode)) {
		return ERR_PTR(-ESTALE);
    }
	inode = iget5_locked(sb, /* our superblock */
			     /*
			      * hashval: we use inode number, but we can
			      * also use "(unsigned long)lower_inode"
			      * instead.
			      */
			     lower_inode->i_ino, /* hashval */
			     xcfs_inode_test,	/* inode comparison function */
			     xcfs_inode_set, /* inode init function */
			     lower_inode); /* data passed to test+set fxns */
	if (!inode) {
		iput(lower_inode);
		return ERR_PTR(-ENOMEM);
	}
	/* if found a cached inode, then just return it (after iput) */
	if (!(inode->i_state & I_NEW)) {
		iput(lower_inode);
		return inode;
	}

	/* initialize new inode */
	info = XCFS_I(inode);

	inode->i_ino = lower_inode->i_ino;
	xcfs_set_lower_inode(inode, lower_inode);

	inode->i_version++;

	/* use different set of inode ops for symlinks & directories */
	if (S_ISDIR(lower_inode->i_mode))
		inode->i_op = &xcfs_inode_dir_ops;
	else if (S_ISLNK(lower_inode->i_mode))
		inode->i_op = &xcfs_inode_sym_ops;
	else
		inode->i_op = &xcfs_inode_file_ops;

	/* use different set of file ops for directories */
	if (S_ISDIR(lower_inode->i_mode))
		inode->i_fop = &xcfs_dir_ops;
	else
		inode->i_fop = &xcfs_file_ops;

	inode->i_mapping->a_ops = &xcfs_addr_ops;

	inode->i_atime.tv_sec = 0;
	inode->i_atime.tv_nsec = 0;
	inode->i_mtime.tv_sec = 0;
	inode->i_mtime.tv_nsec = 0;
	inode->i_ctime.tv_sec = 0;
	inode->i_ctime.tv_nsec = 0;

	/* properly initialize special inodes */
	if (S_ISBLK(lower_inode->i_mode) || S_ISCHR(lower_inode->i_mode) ||
	    S_ISFIFO(lower_inode->i_mode) || S_ISSOCK(lower_inode->i_mode))
		init_special_inode(inode, lower_inode->i_mode,
				   lower_inode->i_rdev);

	/* all well, copy inode attributes */
	fsstack_copy_attr_all(inode, lower_inode);
	fsstack_copy_inode_size(inode, lower_inode);

	unlock_new_inode(inode);
	return inode;
}

/*
 * Helper interpose routine, called directly by ->lookup to handle
 * spliced dentries.
 */
static struct dentry *__xcfs_interpose(struct dentry *dentry,
					 struct super_block *sb,
					 struct path *lower_path)
{
	struct inode *inode;
	struct inode *lower_inode;
	struct super_block *lower_sb;
	struct dentry *ret_dentry;

	lower_inode = d_inode(lower_path->dentry);
	lower_sb = xcfs_lower_super(sb);

	/* check that the lower file system didn't cross a mount point */
	if (lower_inode->i_sb != lower_sb) {
		ret_dentry = ERR_PTR(-EXDEV);
		goto out;
	}

	/*
	 * We allocate our new inode below by calling xcfs_iget,
	 * which will initialize some of the new inode's fields
	 */

	/* inherit lower inode number for xcfs's inode */
	inode = xcfs_iget(sb, lower_inode);
	if (IS_ERR(inode)) {
		ret_dentry = ERR_PTR(PTR_ERR(inode));
		goto out;
	}

	ret_dentry = d_splice_alias(inode, dentry);

out:
	return ret_dentry;
}

/*
 * Connect a xcfs inode dentry/inode with several lower ones.  This is
 * the classic stackable file system "vnode interposition" action.
 *
 * @dentry: xcfs's dentry which interposes on lower one
 * @sb: xcfs's super_block
 * @lower_path: the lower path (caller does path_get/put)
 */
/* copied from wrapfs */
/* this function interposes a dentry into a path */
int xcfs_interpose(struct dentry *dentry, struct super_block *sb,
		     struct path *lower_path)
{
	struct dentry *ret_dentry;

	ret_dentry = __xcfs_interpose(dentry, sb, lower_path);
	return PTR_ERR(ret_dentry);
}

/*
 * Main driver function for xcfs's lookup.
 *
 * Returns: NULL (ok), ERR_PTR if an error occurred.
 * Fills in lower_parent_path with <dentry,mnt> on success.
 */
static struct dentry *__xcfs_lookup(struct dentry *dentry,
				      unsigned int flags,
				      struct path *lower_parent_path)
{
	int err = 0;
	struct vfsmount *lower_dir_mnt;
	struct dentry *lower_dir_dentry = NULL;
	struct dentry *lower_dentry;
	const char *name;
	struct path lower_path;
	struct qstr this;
	struct dentry *ret_dentry = NULL;

	/* must initialize dentry operations */
	d_set_d_op(dentry, &xcfs_dent_ops);

	if (IS_ROOT(dentry)) {
		goto out;
    }

	name = dentry->d_name.name;

	/* now start the actual lookup procedure */
	lower_dir_dentry = lower_parent_path->dentry;
	lower_dir_mnt = lower_parent_path->mnt;

	/* Use vfs_path_lookup to check if the dentry exists or not */
	err = vfs_path_lookup(lower_dir_dentry, lower_dir_mnt, name, 0,
			      &lower_path);

	/* no error: handle positive dentries */
	if (!err) {
		xcfs_set_lower_path(dentry, &lower_path);
		ret_dentry =
			__xcfs_interpose(dentry, dentry->d_sb, &lower_path);
		if (IS_ERR(ret_dentry)) {
			err = PTR_ERR(ret_dentry);
			 /* path_put underlying path on error */
			xcfs_put_reset_lower_path(dentry);
		}
		goto out;
	}

	/*
	 * We don't consider ENOENT an error, and we want to return a
	 * negative dentry.
	 */
	if (err && err != -ENOENT) {
		goto out;
    }

	/* instatiate a new negative dentry */
	this.name = name;
	this.len = strlen(name);
	this.hash = full_name_hash(lower_dir_dentry, this.name, this.len);
	lower_dentry = d_lookup(lower_dir_dentry, &this);
	if (lower_dentry) {
		goto setup_lower;
    }

	lower_dentry = d_alloc(lower_dir_dentry, &this);
	if (!lower_dentry) {
		err = -ENOMEM;
		goto out;
	}
	d_add(lower_dentry, NULL); /* instantiate and hash */

setup_lower:
	lower_path.dentry = lower_dentry;
	lower_path.mnt = mntget(lower_dir_mnt);
	xcfs_set_lower_path(dentry, &lower_path);

	/*
	 * If the intent is to create a file, then don't return an error, so
	 * the VFS will continue the process of making this negative dentry
	 * into a positive one.
	 */
	if (err == -ENOENT || (flags & (LOOKUP_CREATE|LOOKUP_RENAME_TARGET))) {
		err = 0;
    }

out:
	if (err) {
		return ERR_PTR(err);
    }
	return ret_dentry;
}

/* copied from wrapfs */
/* this function looks up a directory in a dentry */
struct dentry *xcfs_lookup(struct inode *dir, struct dentry *dentry,
			     unsigned int flags)
{
	int err;
	struct dentry *ret, *parent;
	struct path lower_parent_path;

	parent = dget_parent(dentry);

	xcfs_get_lower_path(parent, &lower_parent_path);

	/* allocate dentry private data.  We free it in ->d_release */
	err = new_dentry_private_data(dentry);
	if (err) {
		ret = ERR_PTR(err);
		goto out;
	}
	ret = __xcfs_lookup(dentry, flags, &lower_parent_path);
	if (IS_ERR(ret)) {
		goto out;
    }
	if (ret) {
		dentry = ret;
    }
	if (dentry->d_inode) {
		fsstack_copy_attr_times(dentry->d_inode,
					xcfs_lower_inode(dentry->d_inode));
    }
	/* update parent directory's atime */
	fsstack_copy_attr_atime(parent->d_inode,
				xcfs_lower_inode(parent->d_inode));

out:
	xcfs_put_lower_path(parent, &lower_parent_path);
	dput(parent);
	return ret;
}

