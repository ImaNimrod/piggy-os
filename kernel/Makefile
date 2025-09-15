include ../config.mk

CFLAGS := \
		  -g \
		  -std=gnu11 \
		  -O2 \
		  -fms-extensions \
		  -ffreestanding \
		  -fno-stack-protector \
		  -fno-stack-check \
		  -fno-omit-frame-pointer \
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
		   --gc-sections \
		   -z max-page-size=0x1000 \
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

KERNEL_CFILES := $(shell cd src && find . -type f -name '*.c')
KERNEL_ASFILES := $(shell cd src && find . -type f -name '*.S')
KERNEL_OBJFILES := $(addprefix obj/,$(KERNEL_CFILES:.c=.o) $(KERNEL_ASFILES:.S=.o))

.PHONY: all
all: $(KERNEL_FILE)

include/limine.h:
	@cp -v ../limine/limine.h $@

$(KERNEL_FILE): $(KERNEL_OBJFILES)
	@mkdir -p "$$(dirname $@)"
	$(LD) $(LDFLAGS) $(KERNEL_OBJFILES) -o $@

obj/%.o: src/%.c include/limine.h
	@mkdir -p "$$(dirname $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/%.o: src/%.S
	@mkdir -p "$$(dirname $@)"
	$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	$(RM) -r $(KERNEL_FILE) obj

.PHONY: distclean
distclean: clean
	$(RM) include/limine.h
