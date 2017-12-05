#include "xcfs.h"

#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/page-flags.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

int xcfs_encrypt_page(struct page *page, struct page *crypt_page)
{
	struct inode *xcfs_inode = NULL;
	char *temp_page_virt = NULL;
	struct page *temp_page = NULL;
	int retval = 0;
	char *old_page_virt = kmap(page);

	xcfs_inode = page->mapping->host;
	
	//allocate temporary page
	temp_page = alloc_page(GFP_USER);
	if(temp_page == NULL)
	{
		retval = -ENOMEM;
		printk("Error allocation memory for temp encrypted page\n");
		goto xcfs_encrypt_out;
	}

	//map temp page to useable address
	temp_page_virt = kmap(temp_page);
	
	printk("copying old to temp page\n");
	memcpy(temp_page_virt, old_page_virt, PAGE_SIZE);

	//for each extent
	//	do encryption
	//		check failure
	//	write extent to lower page
	//		check failure
	//end for

	retval = 0;
xcfs_encrypt_out:
	//frees and unmaps temporary page, if necessary
	if(temp_page)
	{
		kunmap(temp_page);
		__free_page(temp_page);
	}

	return retval;
}

int xcfs_decrypt_page(struct page *page, struct page *crypt_page)
{
	return 0;
}

static int xcfs_fault(struct vm_fault *vmf)
{
	int err;
	struct file *file, *lower_file;
	const struct vm_operations_struct *lower_vm_ops;
	struct vm_area_struct lower_vma;

	memcpy(&lower_vma, vmf->vma, sizeof(struct vm_area_struct));
	file = lower_vma.vm_file;
	lower_vm_ops = XCFS_F(file)->lower_vm_ops;
	BUG_ON(!lower_vm_ops);

	lower_file = xcfs_lower_file(file);
	/*
	 * XXX: vm_ops->fault may be called in parallel.  Because we have to
	 * resort to temporarily changing the vma->vm_file to point to the
	 * lower file, a concurrent invocation of xcfs_fault could see a
	 * different value.  In this workaround, we keep a different copy of
	 * the vma structure in our stack, so we never expose a different
	 * value of the vma->vm_file called to us, even temporarily.  A
	 * better fix would be to change the calling semantics of ->fault to
	 * take an explicit file pointer.
	 */
	lower_vma.vm_file = lower_file;
	err = lower_vm_ops->fault(vmf);
	return err;
}

static int xcfs_page_mkwrite(struct vm_fault *vmf)
{
	int err = 0;
	struct file *file, *lower_file;
	const struct vm_operations_struct *lower_vm_ops;
	struct vm_area_struct lower_vma;

	memcpy(&lower_vma, vmf->vma, sizeof(struct vm_area_struct));
	file = lower_vma.vm_file;
	lower_vm_ops = XCFS_F(file)->lower_vm_ops;
	BUG_ON(!lower_vm_ops);
	if (!lower_vm_ops->page_mkwrite)
		goto out;

	lower_file = xcfs_lower_file(file);
	/*
	 * XXX: vm_ops->page_mkwrite may be called in parallel.
	 * Because we have to resort to temporarily changing the
	 * vma->vm_file to point to the lower file, a concurrent
	 * invocation of xcfs_page_mkwrite could see a different
	 * value.  In this workaround, we keep a different copy of the
	 * vma structure in our stack, so we never expose a different
	 * value of the vma->vm_file called to us, even temporarily.
	 * A better fix would be to change the calling semantics of
	 * ->page_mkwrite to take an explicit file pointer.
	 */
	lower_vma.vm_file = lower_file;
	err = lower_vm_ops->page_mkwrite(vmf);
out:
	return err;
}

static ssize_t xcfs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	/*
	 * This function should never be called directly.  We need it
	 * to exist, to get past a check in open_check_o_direct(),
	 * which is called from do_last().
	 */
	return -EINVAL;
}

static int xcfs_readpage(struct file *file, struct page *page)
{
	int retval = 0;
    	struct page *crypt_page = NULL;
    	mm_segment_t old_fs;
    	mode_t orig_mode;
    	char *page_data = (char *)kmap(page);
    
    	file->f_pos = page_offset(page);

    	orig_mode = file->f_mode;
    	file->f_mode |= FMODE_READ;

	printk("xcfs_readpage\n");

    	old_fs = get_fs();
    	set_fs(get_ds());
	//retval = xcfs_decrypt_page(page, crypt_page);
    	retval = vfs_read(file, page_data, PAGE_SIZE, 
				&file->f_pos);

	if(retval)
	{
		printk("Error decrypting page: %d\n", retval);
		ClearPageUptodate(page);
	}
	else
	{
		SetPageUptodate(page);
	}

    file->f_mode = orig_mode;
    set_fs(old_fs);
	//printk("Unlocking page with index = [0x%.161lx]\n", page->index);
	unlock_page(page);

	return retval;
}

static int xcfs_writepage(struct page *page, struct writeback_control *wbc)
{
    struct page *crypt_page;
    // TA says we shouldn't directly change the page we are given...
	int retval = xcfs_encrypt_page(page, crypt_page);

	printk("xcfs_writepage\n");

	if(retval)
	{
		//printk("Error encrypting page [0x%161lx]\n", page->index);
		ClearPageUptodate(page);
	}
	else
	{
		SetPageUptodate(page);
	}
	unlock_page(page);
	return retval;
}


const struct address_space_operations xcfs_addr_ops = {
	.readpage 	= xcfs_readpage,
	.writepage 	= xcfs_writepage,
//	.direct_IO 	= xcfs_direct_IO,
};

const struct vm_operations_struct xcfs_vm_ops = {
//	.fault		= xcfs_fault,
//	.page_mkwrite	= xcfs_page_mkwrite,
};
