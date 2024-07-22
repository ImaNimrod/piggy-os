include ../config.mk

CFLAGS := -O2 -std=gnu11 -ffreestanding -fno-stack-protector -fno-stack-check -fno-lto \
		  -fms-extensions -fno-omit-frame-pointer -fno-strict-aliasing -fpie \
		  -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-3dnow -mno-sse -mno-sse2 -mno-red-zone \
		  -Wall -Wextra -pedantic -Wpointer-arith -Wcast-align -Winline -Wno-long-long \
		  -Wnested-externs -Wstrict-prototypes -Wredundant-decls -Wshadow -Wvla

CPPFLAGS := -I include -D GIT_VERSION=\"$(shell git describe --always --tags)\"

ASFLAGS := -f elf64

LDFLAGS := -m elf_x86_64 -nostdlib -static -pie \
		   --no-dynamic-linker -z text -z max-page-size=0x1000

KERNEL_CFILES := $(shell cd src && find . -type f -name '*.c')
KERNEL_ASFILES := $(shell cd src && find . -type f -name '*.asm')
KERNEL_OBJFILES := $(addprefix obj/,$(KERNEL_CFILES:.c=.o) $(KERNEL_ASFILES:.asm=.o))

.PHONY: all
all: $(KERNEL_FILE)

include/limine.h:
	@curl -Lo $@ https://github.com/limine-bootloader/limine/raw/trunk/limine.h

$(KERNEL_FILE): linker.ld $(KERNEL_OBJFILES)
	@mkdir -p "$$(dirname $@)"
	@echo "  LD   $@"
	@$(LD) $(LDFLAGS) $(KERNEL_OBJFILES) -T linker.ld -o $@

obj/%.o: src/%.c include/limine.h
	@mkdir -p "$$(dirname $@)"
	@echo "  CC   $(shell basename $<)"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

obj/%.o: src/%.asm
	@mkdir -p "$$(dirname $@)"
	@echo "  AS   $(shell basename $<)"
	@$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	$(RM) -r $(KERNEL_FILE) obj

.PHONY: distclean
distclean:
	$(RM) include/limine.h
