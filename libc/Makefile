include ../config.mk

CFLAGS := -Wall -Wextra -Wno-long-long -Wstrict-prototypes -Wpointer-arith -Wvla -Wshadow \
		  -pedantic -std=gnu11 -mgeneral-regs-only

ASFLAGS := -f elf64

LIBC_CFILES := $(shell cd src && find . -type f -name '*.c')
LIBC_ASFILES := $(shell cd src && find . -type f -name '*.asm')
LIBC_OBJFILES := $(addprefix obj/,$(LIBC_ASFILES:.asm=.o) $(LIBC_CFILES:.c=.o))

LIBC_CRT_OBJFILES := $(wildcard obj/crt/*.o)

.PHONY: libc
libc: install-headers $(SYSROOT_DIR)/lib/libc.a | install-crt

$(SYSROOT_DIR)/lib/libc.a: $(LIBC_OBJFILES)
	@echo "  AR   $@"
	@$(AR) -cr $@ $(LIBC_OBJFILES)

obj/%.o: src/%.c
	@mkdir -p "$$(dirname $@)"
	@echo "  CC   $(shell basename $<)"
	@$(CC) $(CFLAGS) -c $< -o $@

obj/%.o: src/%.asm
	@mkdir -p "$$(dirname $@)"
	@echo "  AS   $(shell basename $<)"
	@$(AS) $(ASFLAGS) $< -o $@

.PHONY: install-crt
install-crt:
	@echo "installing crt objects in sysroot..."
	cp -fr obj/crt/crt*.o $(SYSROOT_DIR)/lib

.PHONY: install-headers
install-headers:
	@echo "installing headers in sysroot..."
	cp -fr include/* $(SYSROOT_DIR)/usr/include

.PHONY: clean
clean:
	$(RM) -r obj
	$(RM) -r $(SYSROOT_DIR)/lib/libc.a $(SYSROOT_DIR)/lib/crt*.o
