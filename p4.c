#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

static int __init p4_init(void)
{
	printk("Loading module: p4\n");
	return 0;
}

static void __exit p4_exit(void)
{
	printk("Unloading module p4\n");
}

module_init(p4_init);
module_exit(p4_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Monger <mpm8128@vt.edu>");
MODULE_DESCRIPTION("XCFS - encrypted filesystem");
