MODULES_DIR := /lib/modules/$(shell uname -r)
KERNEL_DIR := ${MODULES_DIR}/build

all:
	make -C ${KERNEL_DIR} M=$$PWD;
modules:
	make -C ${KERNEL_DIR} M=$$PWD $@;
modules_install:
	make -C ${KERNEL_DIR} M=$$PWD $@;
clean:
	make -C ${KERNEL_DIR} M=$$PWD $@;
	rm -f ../common/*.o
	rm -f ../common/rfc6145/*.o
debug:
	make -C ${KERNEL_DIR} M=$$PWD JOOL_DEBUG=-DDEBUG;

# The targets below are meant for quick testing.
# If you want to install the module, see ../INSTALL.

insert:
	sudo dmesg -C
	sudo insmod jool_siit.ko pool6=2001:db8:100::/40
	dmesg

remove:
	-sudo rmmod jool_siit.ko
	dmesg | tail -1
