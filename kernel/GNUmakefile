MAKEFLAGS += -rR
.SUFFIXES:

override KERNEL_FILE := kernel.elf

CFLAGS := \
		  -std=gnu11 \
		  -g \
		  -O2 \
		  -fms-extensions \
		  -ffreestanding \
		  -fno-stack-protector \
		  -fno-stack-check \
		  -fno-PIC \
		  -ffunction-sections \
		  -fdata-sections \
		  -Wall \
		  -Wextra \
		  -Wshadow \
		  -Winline \
		  -Wstrict-prototypes \
		  -Wnested-externs \
		  -Wredundant-decls \
		  -pedantic \
		  -m64 \
		  -march=x86-64 \
		  -mno-80387 \
		  -mno-mmx \
		  -mno-sse \
		  -mno-sse2 \
		  -mno-red-zone \
		  -mcmodel=kernel

LDFLAGS := \
		   -m elf_x86_64 \
		   --build-id=none \
		   -nostdlib \
		   -static \
		   -z max-page-size=0x1000 \
		   --gc-sections \
		   -T linker.ld

CPPFLAGS := \
			-I include \
			-DLIMINE_API_REVISION=3 \
			-DPRINTF_DISABLE_SUPPORT_EXPONENTIAL \
			-DPRINTF_DISABLE_SUPPORT_FLOAT \
			-DPRINTF_DISABLE_SUPPORT_LONG_LONG \
			-DFLANTERM_FB_DISABLE_BUMP_ALLOC \
			-MMD \
			-MP

NASMFLAGS := -f elf64

KERNEL_CFILES := $(shell cd src && find . -type f -name '*.c')
KERNEL_ASFILES := $(shell cd src && find . -type f -name '*.asm')
KERNEL_OBJFILES := $(addprefix obj/,$(KERNEL_CFILES:.c=.o) $(KERNEL_ASFILES:.asm=.o))

.PHONY: all
all: $(KERNEL_FILE)

include/limine.h:
	@curl -Lo $@ https://github.com/limine-bootloader/limine/raw/trunk/limine.h

$(KERNEL_FILE): $(KERNEL_OBJFILES)
	@mkdir -p "$$(dirname $@)"
	ld $(LDFLAGS) $(KERNEL_OBJFILES) -o $@

obj/%.o: src/%.c include/limine.h
	@mkdir -p "$$(dirname $@)"
	cc $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/%.o: src/%.asm
	@mkdir -p "$$(dirname $@)"
	nasm $(NASMFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -rf $(KERNEL_FILE) obj

.PHONY: distclean
distclean: clean
	rm -f include/limine.h
