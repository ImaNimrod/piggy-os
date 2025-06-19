MAKEFLAGS += -rR
.SUFFIXES:

override IMAGE_NAME := piggy

EMUOPTS := -machine q35 \
		   -m 4G \
		   -cpu host \
		   -enable-kvm \
		   -smp 2 \
		   -serial stdio \
		   -bios /usr/share/edk2/x64/OVMF.4m.fd

.PHONY: all
all: $(IMAGE_NAME).iso

.PHONY: run
run: run-virtio

.PHONY: run-virtio
run-virtio:
	qemu-system-x86_64 $(EMUOPTS) \
		-drive id=disk,file=disk.img,format=raw,if=none -device virtio-blk-pci,drive=disk \
		-netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
		-device virtio-net-pci,netdev=net0,mac=52:54:00:12:34:56 \
		-cdrom $(IMAGE_NAME).iso

.PHONY: run-realhw
run-realhw:
	qemu-system-x86_64 $(EMUOPTS) \
		-hda disk.img \
		-netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
		-device e1000e,netdev=net0,mac=52:54:00:12:34:56 \
		-cdrom $(IMAGE_NAME).iso

.PHONY: todolist
todolist:
	@echo -e "List of todos and fixme in sources: \n"
	-@grep -FHr -e TODO -e FIXME kernel

limine/limine:
	$(MAKE) -C limine CC="cc" CFLAGS="-g -O2 -pipe"

.PHONY: kernel
kernel:
	$(MAKE) -C kernel

$(IMAGE_NAME).iso: limine/limine kernel
	rm -rf iso_root
	mkdir -p iso_root/boot
	cp -v kernel/kernel.elf test/test iso_root/boot/
	mkdir -p iso_root/boot/limine
	cp -v limine.conf iso_root/boot/limine/
	mkdir -p iso_root/EFI/BOOT
	cp -v limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/boot/limine/
	cp -v limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp -v limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
	./limine/limine bios-install $(IMAGE_NAME).iso
	rm -rf iso_root

.PHONY: clean
clean:
	$(MAKE) -C kernel clean
	rm -rf iso_root $(IMAGE_NAME).iso

.PHONY: distclean
distclean:
	$(MAKE) -C kernel distclean
	rm -rf *.iso iso_root limine
