include ../config.mk

CFILES := $(wildcard *.c)
ASMFILES := $(wildcard *.asm)
INITOBJ := $(CFILES:.c=.o) $(ASMFILES:.asm=.o)

ASFLAGS := -felf64
CFLAGS := -ffreestanding -fno-stack-protector -mno-red-zone -Wall -Wextra -pedantic -std=gnu11 -O0

all: init

init: $(INITOBJ)
	$(LD) $(LDFLAGS) -T linker.ld $^ -o ../initrd/bin/init

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	$(RM) -r $(INITOBJ)
