#ifndef _XCFS_H_
#define _XCFS_H_

#include <linux/dcache.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/aio.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/seq_file.h>
#include <linux/statfs.h>
#include <linux/fs_stack.h>
#include <linux/magic.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/xattr.h>
#include <linux/exportfs.h>
#include <linux/module.h>

#define XCFS_MAGIC_NUMBER 	0x69
#define CURRENT_TIME		1000
#define XCFS_NAME           "xcfs"
#define PRINT_PREF KERN_INFO "[xcfs]: "

void xcfs_decrypt(char* buf, size_t count);
void xcfs_encrypt(char* buf, size_t count);

/* operations vectors defined in specific files */
extern const struct file_operations xcfs_file_ops;
extern const struct file_operations xcfs_dir_ops;
extern const struct inode_operations xcfs_inode_file_ops;
extern const struct inode_operations xcfs_inode_dir_ops;
extern const struct inode_operations xcfs_inode_sym_ops;
extern const struct super_operations xcfs_sb_ops;
extern const struct dentry_operations xcfs_dent_ops;
extern const struct address_space_operations xcfs_addr_ops, xcfs_dummy_addr_ops;
extern const struct vm_operations_struct xcfs_vm_ops;
extern const struct export_operations xcfs_export_ops;
extern const struct xattr_handler *xcfs_xattr_handlers[];

extern int xcfs_init_inode_cache(void);
extern void xcfs_destroy_inode_cache(void);
extern int xcfs_init_dentry_cache(void);
extern void xcfs_destroy_dentry_cache(void);
extern int new_dentry_private_data(struct dentry *dentry);
extern void free_dentry_private_data(struct dentry *dentry);
extern struct dentry *xcfs_lookup(struct inode *dir, struct dentry *dentry,
				    unsigned int flags);
extern struct inode *xcfs_iget(struct super_block *sb,
				 struct inode *lower_inode);
extern int xcfs_interpose(struct dentry *dentry, struct super_block *sb,
			    struct path *lower_path);

/* vfs_path_lookup is exported but not included in any headers */
int vfs_path_lookup(struct dentry *dentry, struct vfsmount *mnt,
                const char *name, unsigned int flags,
                struct path *path);

/* vfs_listxattr is exported but not included in any headers */
ssize_t vfs_listxattr(struct dentry *dentry, char *list, size_t size);

/* vfs_removexattr is exported but not included in any headers */
int vfs_removexattr(struct dentry *dentry, const char *name);

/* copied from wrapfs and modified */
/* deals with private data of different structs */

/* file private data */
struct xcfs_file_info {
	struct file *lower_file;
	const struct vm_operations_struct *lower_vm_ops;
};

/* xcfs inode data in memory */
struct xcfs_inode_info {
	struct inode *lower_inode;
	struct inode vfs_inode;
};

/* xcfs dentry data in memory */
struct xcfs_dentry_info {
	spinlock_t lock;	/* protects lower_path */
	struct path lower_path;
};

/* xcfs super-block data in memory */
struct xcfs_sb_info {
	struct super_block *lower_sb;
};

/*
 * inode to private data
 *
 * Since we use containers and the struct inode is _inside_ the
 * xcfs_inode_info structure, XCFS_I will always (given a non-NULL
 * inode pointer), return a valid non-NULL pointer.
 */
static inline struct xcfs_inode_info *XCFS_I(const struct inode *inode)
{
	return container_of(inode, struct xcfs_inode_info, vfs_inode);
}

/* dentry to private data */
#define XCFS_D(dent) ((struct xcfs_dentry_info *)(dent)->d_fsdata)

/* superblock to private data */
#define XCFS_SB(super) ((struct xcfs_sb_info *)(super)->s_fs_info)

/* file to private Data */
#define XCFS_F(file) ((struct xcfs_file_info *)((file)->private_data))

/* file to lower file */
static inline struct file *xcfs_lower_file(const struct file *f)
{
	return XCFS_F(f)->lower_file;
}

static inline void xcfs_set_lower_file(struct file *f, struct file *val)
{
	XCFS_F(f)->lower_file = val;
}

/* inode to lower inode. */
static inline struct inode *xcfs_lower_inode(const struct inode *i)
{
	return XCFS_I(i)->lower_inode;
}

static inline void xcfs_set_lower_inode(struct inode *i, struct inode *val)
{
	XCFS_I(i)->lower_inode = val;
}

/* superblock to lower superblock */
static inline struct super_block *xcfs_lower_super(
	const struct super_block *sb)
{
	return XCFS_SB(sb)->lower_sb;
}

static inline void xcfs_set_lower_super(struct super_block *sb,
					  struct super_block *val)
{
	XCFS_SB(sb)->lower_sb = val;
}

/* path based (dentry/mnt) macros */
static inline void pathcpy(struct path *dst, const struct path *src)
{
	dst->dentry = src->dentry;
	dst->mnt = src->mnt;
}
/* Returns struct path.  Caller must path_put it. */
static inline void xcfs_get_lower_path(const struct dentry *dent,
					 struct path *lower_path)
{
	spin_lock(&XCFS_D(dent)->lock);
	pathcpy(lower_path, &XCFS_D(dent)->lower_path);
	path_get(lower_path);
	spin_unlock(&XCFS_D(dent)->lock);
	return;
}
static inline void xcfs_put_lower_path(const struct dentry *dent,
					 struct path *lower_path)
{
	path_put(lower_path);
	return;
}
static inline void xcfs_set_lower_path(const struct dentry *dent,
					 struct path *lower_path)
{
	spin_lock(&XCFS_D(dent)->lock);
	pathcpy(&XCFS_D(dent)->lower_path, lower_path);
	spin_unlock(&XCFS_D(dent)->lock);
	return;
}
static inline void xcfs_reset_lower_path(const struct dentry *dent)
{
	spin_lock(&XCFS_D(dent)->lock);
	XCFS_D(dent)->lower_path.dentry = NULL;
	XCFS_D(dent)->lower_path.mnt = NULL;
	spin_unlock(&XCFS_D(dent)->lock);
	return;
}
static inline void xcfs_put_reset_lower_path(const struct dentry *dent)
{
	struct path lower_path;
	spin_lock(&XCFS_D(dent)->lock);
	pathcpy(&lower_path, &XCFS_D(dent)->lower_path);
	XCFS_D(dent)->lower_path.dentry = NULL;
	XCFS_D(dent)->lower_path.mnt = NULL;
	spin_unlock(&XCFS_D(dent)->lock);
	path_put(&lower_path);
	return;
}

/* locking helpers */

static inline struct dentry *lock_parent(struct dentry *dentry)
{
	struct dentry *dir = dget_parent(dentry);
	inode_lock_nested(dir->d_inode, I_MUTEX_PARENT);
	return dir;
}

static inline void unlock_dir(struct dentry *dir)
{
	inode_unlock(dir->d_inode);
	dput(dir);
}

#endif	/* not _XCFS_H_ */
