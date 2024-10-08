SYSROOT_DIR=$(PWD)/sysroot
TOOLCHAIN_DIR=$(PWD)/toolchain

AS=$(TOOLCHAIN_DIR)/build/bin/x86_64-piggy-as
CC=$(TOOLCHAIN_DIR)/build/bin/x86_64-piggy-gcc
LD=$(TOOLCHAIN_DIR)/build/bin/x86_64-piggy-ld
AR=$(TOOLCHAIN_DIR)/build/bin/x86_64-piggy-ar
EMU=$(TOOLCHAIN_DIR)/build/bin/qemu-system-x86_64

IMAGE_NAME=piggy-os.iso
KERNEL_FILE=kernel.elf
INITRD_FILE=initrd.tar
