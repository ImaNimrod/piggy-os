SYSROOT_DIR:=$(PWD)/sysroot
TOOLCHAIN_DIR:=$(PWD)/toolchain

CC:=$(TOOLCHAIN_DIR)/build/bin/x86_64-piggy-gcc
LD:=$(TOOLCHAIN_DIR)/build/bin/x86_64-piggy-ld
AS:=$(TOOLCHAIN_DIR)/build/bin/x86_64-piggy-as
EMU:=$(TOOLCHAIN_DIR)/build/bin/qemu-system-x86_64

IMAGE_NAME:=piggy

KERNEL_FILE:=kernel.elf
INITRD_FILE:=initrd.tar
