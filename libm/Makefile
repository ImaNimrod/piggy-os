
include ../config.mk

CFLAGS := -Wall -Wextra -Wno-long-long -Wstrict-prototypes -Wpointer-arith -Wvla -Wshadow \
		  -pedantic -std=gnu11

LIBM_CFILES := $(shell cd src && find . -type f -name '*.c')
LIBM_ASFILES := $(shell cd src && find . -type f -name '*.S')
LIBM_OBJFILES := $(addprefix obj/,$(LIBM_ASFILES:.S=.o) $(LIBM_CFILES:.c=.o))

.PHONY: libc
libc: install-headers $(SYSROOT_DIR)/lib/libm.a

$(SYSROOT_DIR)/lib/libm.a: $(LIBM_OBJFILES)
	@echo "  AR   $@"
	@$(AR) -cr $@ $(LIBM_OBJFILES)

obj/%.o: src/%.c
	@mkdir -p "$$(dirname $@)"
	@echo "  CC   $(shell basename $<)"
	@$(CC) $(CFLAGS) -c $< -o $@

obj/%.o: src/%.S
	@mkdir -p "$$(dirname $@)"
	@echo "  AS   $(shell basename $<)"
	@$(AS) $(ASFLAGS) $< -o $@

.PHONY: install-headers
install-headers:
	@echo "installing libm headers in sysroot..."
	cp -fr include/* $(SYSROOT_DIR)/usr/include

.PHONY: clean
clean:
	$(RM) -r obj
	$(RM) -r $(SYSROOT_DIR)/lib/libm.a
