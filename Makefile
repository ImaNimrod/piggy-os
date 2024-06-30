include ./config.mk

EMUOPTS=-M q35 -m 2G -serial stdio -no-reboot -bios /usr/share/edk2/x64/OVMF.fd

.PHONY: all
all: $(IMAGE_NAME)

$(IMAGE_NAME): limine kernel userspace initrd
	rm -rf iso_root
	mkdir -p iso_root
	cp -v kernel/bin/$(KERNEL_NAME) $(RAMDISK_NAME) limine.cfg limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/
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

.PHONY: kernel
kernel:
	$(MAKE) -C kernel

limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v7.x-binary --depth=1
	$(MAKE) -C limine

.PHONY: initrd
initrd:
	cd initrd; tar -cvf ../$(RAMDISK_NAME) *

.PHONY: userspace
userspace:
	$(MAKE) -C userspace

.PHONY: run
run:
	$(EMU) $(EMUOPTS) -cdrom $(IMAGE_NAME)

.PHONY: run-kvm
run-kvm:
	$(EMU) $(EMUOPTS) -enable-kvm -cpu host -cdrom $(IMAGE_NAME)

.PHONY: todolist
todolist:
	@echo -e "List of todos and fixme in sources: \n"
	-@grep -FHr -e TODO -e FIXME kernel userspace

.PHONY: clean
clean:
	$(RM) -r $(IMAGE_NAME) $(RAMDISK_NAME) iso_root
	$(MAKE) -C kernel clean
	$(MAKE) -C userspace clean

.PHONY: distclean
distclean:
	$(RM) -r limine
	$(MAKE) -C kernel distclean
