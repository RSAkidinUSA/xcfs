#include "xcfs.h"

#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/page-flags.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

//Reading and Decryption
void xcfs_decrypt(char* buf, size_t count) 
{
	int i = 0;
	printk("xcfs_decrypt\n");
	for(i = 0; i < count; ++i) {
		buf[i]--;
	}
}

int xcfs_decrypt_page(struct file *file, struct page *page)
{
	char* virt = kmap(page);

	printk("xcfs_decrypt_page\n");

	xcfs_decrypt(virt, PAGE_SIZE);

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

	if(rc > 0)	//positive values mean we read correctly,
		rc = 0;	//	so this function should return 0

	//some cleanup
	kunmap(page);
	flush_dcache_page(page);
	return rc;
}

//returns 0 on success, nonzero on failure
static int xcfs_readpage(struct file *file, struct page *page)
{
	int rc = 0;

	printk("xcfs_readpage\n");

	rc = read_lower_page_segment(file, page, page->index, 0,
					PAGE_SIZE);

	//do decryption
	xcfs_decrypt_page(file, page);

	if(rc)
		ClearPageUptodate(page);
	else
		SetPageUptodate(page);

	unlock_page(page);
	printk("xcfs_readpage returns %d\n", rc);
	return rc;
}

//Writing and Encryption
void xcfs_encrypt(char* buf, size_t count) 
{
	int i = 0;
	printk("xcfs_encrypt\n");
	for(i = 0; i < count; ++i) {
		buf[i]++;
	}
}

int xcfs_encrypt_page(struct page *page, struct page *crypt_page)
{
	char *old_page_virt = kmap(page);
	char *crypt_page_virt = kmap(crypt_page);

	printk("xcfs_encrypt_page\n");	
	
	//copies old page to temp page
	memcpy(crypt_page_virt, old_page_virt, PAGE_SIZE);

	xcfs_encrypt(crypt_page_virt, PAGE_SIZE);

	return 0;
}

static int xcfs_writepage(struct page *page, struct writeback_control *wbc)
{
    struct page *crypt_page = NULL;
	struct inode *lower_inode = xcfs_lower_inode(page->mapping->host);
	int retval = 0;
	
	printk("xcfs_writepage\n");
	
	//allocate temporary page
	crypt_page = alloc_page(GFP_USER);
	if(crypt_page == NULL)
	{
		retval = -ENOMEM;
		printk("Error allocation memory for temp encrypted page\n");
		goto xcfs_writepage_out;
	}

	//encrypt the page given to us and store it in a temporary page
	xcfs_encrypt_page(page, crypt_page);

	//passes the writepage call down to the lower filesystem,
	//	using the encrypted page
	//we can't pass it down to kernel_write like we do in readpage,
	//	because there's no way to get the "struct file" data structure
	//	for the page we're given without making some modifications
	//	to the module's data structures (see the ecrypfs_inode_info 
	//	struct)
	//(we could probably implement readpage in the same way, but it works
	//	as-is, and I don't want to mess with it)
	retval = lower_inode->i_mapping->a_ops->writepage(crypt_page, wbc);

xcfs_writepage_out:
	//frees temporary page, if necessary
	if(crypt_page)
	{
		__free_page(crypt_page);
	}
	
	if(retval)
	{
		printk("xcfs_encrypt_page: error encrypting page\n");
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
};
