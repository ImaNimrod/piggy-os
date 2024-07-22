include ../config.mk

CFLAGS := -Wall -Wextra -pedantic -O0

USERSPACE_PROGRAMS := $(patsubst %.c, $(SYSROOT_DIR)/bin/%, $(wildcard *.c))

.PHONY: userspace
userspace: $(USERSPACE_PROGRAMS)

$(SYSROOT_DIR)/bin/%: %.c
	@echo "  CC   $(shell basename $@)"
	@$(CC) $(CFLAGS) -o $@ $<

.PHONY:
clean:
	$(RM) $(USERSPACE_PROGRAMS)
