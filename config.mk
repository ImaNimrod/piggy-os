TOOLCHAIN_DIR=$(PWD)/toolchain

CC=$(TOOLCHAIN_DIR)/build/bin/x86_64-elf-gcc
LD=$(TOOLCHAIN_DIR)/build/bin/x86_64-elf-ld
AS=$(TOOLCHAIN_DIR)/build/bin/x86_64-elf-as
EMU=$(TOOLCHAIN_DIR)/build/bin/qemu-system-x86_64
