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

/* encrypt and decrypt functions */
static void xcfs_decrypt(char* buf, size_t count) 
{
	int i = 0;
	printk("xcfs_decrypt\n");
	for(i = 0; i < count; ++i) {
		buf[i]--;
	}
}

int xcfs_decrypt_page(struct file *file, struct page *page)
{
	char* virt = NULL;
	struct file* lower_file = xcfs_lower_file(file);
//	int count = lower_file->f_inode->i_size;

	printk("xcfs_decrypt_page\n");

	virt = kmap(page);

	xcfs_decrypt(virt, PAGE_SIZE);

	printk("virt = %s\n", virt);

	//some cleanup
	kunmap(page);
	
	return 0;
}


//returns number of bytes read (positive) or an error (negative)
static int read_lower(struct file* file, char *data, loff_t offset, size_t size)
{
	struct file *lower_file = NULL;
	printk("read_lower\n");
	lower_file = xcfs_lower_file(file);
	if(!lower_file)
		return -EIO;
	return kernel_read(lower_file, offset, data, size);
}

//returns 0 on success, nonzero on failure
static int read_lower_page_segment(	struct file *file,
					struct page *page, pgoff_t page_index,
					size_t offset_in_page, size_t size)
{
	char *virt = NULL;
	loff_t offset = 0;
	int rc = 0;

	printk("read_lower_page_segment\n");

	//calculate file offset from page offset and page index
	offset = ((((loff_t)page_index) << PAGE_SHIFT) + offset_in_page);
	virt = kmap(page);
	
	//hand off actual reading
	rc = read_lower(file, virt, offset, size);

	if(rc > 0)	//positive return values mean we read correctly
		rc = 0;

	//some cleanup
	kunmap(page);
	flush_dcache_page(page);
	return rc;
}

//returns 0 on success, nonzero on failure
static int xcfs_readpage(struct file *file, struct page *page)
{
	int rc = 0;
	int err = 0;	

	printk("xcfs_readpage\n");

	rc = read_lower_page_segment(file, page, page->index, 0,
					PAGE_SIZE);

	//do decryption
	err = xcfs_decrypt_page(file, page);
	//end decryption

	if(rc)
		ClearPageUptodate(page);
	else
		SetPageUptodate(page);

	unlock_page(page);
	printk("xcfs_readpage returns %d\n", rc);
	return rc;
}



static int xcfs_writepage(struct page *page, struct writeback_control *wbc)
{
    	struct page *crypt_page = NULL;
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
