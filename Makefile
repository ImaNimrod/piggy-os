include ./config.mk

EMUOPTS=-M q35 -smp 4 -m 4G -rtc base=localtime \
		-bios /usr/share/edk2/x64/OVMF.fd \
		-audiodev pipewire,id=speaker -machine pcspk-audiodev=speaker \
		-debugcon stdio

.PHONY: all
all: $(IMAGE_NAME)

.NOTPARALLEL:
$(IMAGE_NAME): limine kernel libc userspace initrd
	rm -rf iso_root
	mkdir -p iso_root/boot/limine
	cp -v kernel/$(KERNEL_FILE) iso_root/boot/
	cp -v $(INITRD_FILE) iso_root/boot/
	cp -v limine.conf limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/boot/limine
	mkdir -p iso_root/EFI/BOOT
	cp -v limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp -v limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $@
	./limine/limine bios-install $@
	rm -rf iso_root

.PHONY: limine
limine:
	$(MAKE) -C limine

.PHONY: kernel
kernel:
	$(MAKE) -C kernel

.PHONY: libc
libc:
	$(MAKE) -C libc
	$(MAKE) -C libm

.PHONY: userspace
userspace:
	$(MAKE) -C userspace

.PHONY: initrd
initrd:
	cd $(SYSROOT_DIR); tar -cf ../$(INITRD_FILE) *

.PHONY: toolchain
toolchain:
	$(MAKE) -C libc install-headers
	$(MAKE) -C libm install-headers
	./toolchain/build-toolchain.sh

.PHONY: run
run:
	$(EMU) $(EMUOPTS) -cdrom $(IMAGE_NAME)

.PHONY: run-kvm
run-kvm:
	$(EMU) $(EMUOPTS) -enable-kvm -cpu host -cdrom $(IMAGE_NAME)

.PHONY: todolist
todolist:
	@echo -e "List of todos and fixme in sources: \n"
	-@grep -FHr -e TODO -e FIXME kernel libc userspace

.PHONY: clean
clean:
	$(RM) -r $(IMAGE_NAME) $(INITRD_FILE) iso_root
	$(MAKE) -C kernel clean
	$(MAKE) -C libc clean
	$(MAKE) -C libm clean
	$(MAKE) -C userspace clean

.PHONY: distclean
distclean:
	$(MAKE) -C limine clean
	$(MAKE) -C kernel distclean
