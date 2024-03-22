include ./config.mk

.PHONY: all
all: $(IMAGE_NAME)

$(IMAGE_NAME): limine kernel
	rm -rf iso_root
	mkdir -p iso_root
	cp -v kernel/bin/$(KERNEL_NAME) limine.cfg limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/
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

.PHONY: run
run:
	$(EMU) -M q35 -m 2G -serial stdio -enable-kvm -cpu host -bios /usr/share/edk2/x64/OVMF.fd -cdrom piggy-os.iso -no-reboot -smp 4

.PHONY: clean
clean:
	$(RM) -r $(IMAGE_NAME) iso_root
	$(MAKE) -C kernel clean

.PHONY: distclean
distclean:
	$(RM) -r limine
	$(MAKE) -C kernel distclean
