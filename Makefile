include ./config.mk

EMUOPTS=-M q35 -smp 2 -m 2G -no-reboot \
		-bios /usr/share/edk2/x64/OVMF.fd \
		-serial file:qemu.log \
		-serial mon:stdio

.PHONY: all
all: $(IMAGE_NAME)

$(IMAGE_NAME): limine kernel libc userspace initrd
	rm -rf iso_root
	mkdir -p iso_root
	cp -v kernel/bin/$(KERNEL_NAME) $(INITRD_NAME) limine.cfg limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/
	mkdir -p iso_root/EFI/BOOT
	cp -v limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp -v limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -b limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $@
	./limine/limine bios-install $@
	rm -rf iso_root

limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v7.x-binary --depth=1
	$(MAKE) -C limine

.PHONY: kernel
kernel:
	$(MAKE) -C kernel

.PHONY: libc
libc:
	$(MAKE) -C libc

.PHONY: userspace
userspace:
	$(MAKE) -C userspace

.PHONY: initrd
initrd:
	cd $(SYSROOT_DIR); tar -cf ../$(INITRD_NAME) *

.PHONY: toolchain
toolchain:
	$(MAKE) -C libc install-headers
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
	$(RM) -r $(IMAGE_NAME) $(INITRD_NAME) iso_root
	$(MAKE) -C kernel clean
	$(MAKE) -C libc clean
	$(MAKE) -C userspace clean

.PHONY: distclean
distclean:
	$(RM) -r limine
	$(MAKE) -C kernel distclean
