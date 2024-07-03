SYSROOT_DIR=$(PWD)/sysroot
TOOLCHAIN_DIR=$(PWD)/toolchain

CC=$(TOOLCHAIN_DIR)/build/bin/x86_64-piggy-gcc
LD=$(TOOLCHAIN_DIR)/build/bin/x86_64-piggy-ld
AR=$(TOOLCHAIN_DIR)/build/bin/x86_64-piggy-ar
AS=nasm
EMU=qemu-system-x86_64

IMAGE_NAME=piggy-os.iso
KERNEL_NAME=kernel.elf
INITRD_NAME=initrd.tar
