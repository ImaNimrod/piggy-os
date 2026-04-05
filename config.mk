SYSROOT_DIR:=$(PWD)/sysroot
TOOLCHAIN_DIR:=$(PWD)/toolchain

CC:=$(TOOLCHAIN_DIR)/local/bin/x86_64-piggy-gcc
LD:=$(TOOLCHAIN_DIR)/local/bin/x86_64-piggy-ld
AS:=$(TOOLCHAIN_DIR)/local/bin/x86_64-piggy-as
EMU:=$(TOOLCHAIN_DIR)/local/bin/qemu-system-x86_64
