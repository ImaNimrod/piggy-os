include ../config.mk

CFLAGS := -Os -std=c99 -ffreestanding -fno-stack-protector -fno-stack-check -fno-lto \
		  -fms-extensions -fno-omit-frame-pointer -fno-strict-aliasing \
		  -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-3dnow -mno-sse -mno-sse2 -mno-red-zone \
		  -Wall -Wextra -pedantic -Wpointer-arith -Wcast-align -Winline -Wno-long-long \
		  -Wnested-externs -Wstrict-prototypes -Wredundant-decls -Wshadow

CPPFLAGS := -I include

ASFLAGS := -f elf64

LDFLAGS := -m elf_x86_64 -nostdlib -static -pie \
		   --no-dynamic-linker -z text -z max-page-size=0x1000

override CFILES := $(shell cd src && find -type f -name '*.c')
override ASFILES := $(shell cd src && find -type f -name '*.asm')
override OBJFILES := $(addprefix obj/,$(CFILES:.c=.o) $(ASFILES:.asm=.o))

.PHONY: all
all: bin/$(KERNEL_NAME)

include/limine.h:
	@curl -Lo $@ https://github.com/limine-bootloader/limine/raw/trunk/limine.h

bin/$(KERNEL_NAME): linker.ld $(OBJFILES)
	@mkdir -p "$$(dirname $@)"
	@echo "  LD   $@"
	@$(LD) $(LDFLAGS) $(OBJFILES) -T linker.ld -o $@

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
	$(RM) -r bin obj

.PHONY: distclean
distclean:
	$(RM) include/limine.h
