obj-m := xcfs.o
xcfs-objs := dentry.o file.o inode.o lookup.o main.o mmap.o super.o

CONFIG_MODULE_SIG=n

#this should be the path to your kernel source directory
KDIR := ~/linux

PWD := $(shell pwd)

all: module

module:
	make -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	make -C $(KDIR) SUBDIRS=$(PWD) clean
