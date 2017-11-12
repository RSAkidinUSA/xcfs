obj-m := xcfs.o
xcfs-objs := p4.o

CONFIG_MODULE_SIG=n

#this should be the path to your kernel source directory
KDIR := ~/linux

PWD := $(shell pwd)

all: p4.c
	make -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	make -C $(KDIR) SUBDIRS=$(PWD) clean
