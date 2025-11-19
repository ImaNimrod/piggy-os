include ./config.mk

EMUOPTS := -machine q35 \
		   -m 4G \
		   -cpu host \
		   -enable-kvm \
		   -smp 2 \
		   -no-reboot \
		   -serial stdio \
		   -bios /usr/share/edk2/x64/OVMF.4m.fd

export PATH := $(PATH):$(TOOLCHAIN_DIR)/build/bin

.PHONY: all
all: $(IMAGE_NAME).iso

.PHONY: run
run: run-virtio

.PHONY: run-minimal
run-minimal:
	$(EMU) $(EMUOPTS) \
		-nic none \
		-cdrom $(IMAGE_NAME).iso

.PHONY: run-realhw
run-realhw:
	$(EMU) $(EMUOPTS) \
		-drive file=disk.img,format=raw,if=none,id=disk \
		-device nvme,drive=disk,serial=12345678 \
		-netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
		-device e1000e,netdev=net0,mac=52:54:00:12:34:56 \
		-cdrom $(IMAGE_NAME).iso

.PHONY: run-virtio
run-virtio:
	$(EMU) $(EMUOPTS) \
		-drive id=disk,file=disk.img,format=raw,if=none -device virtio-blk-pci,drive=disk \
		-netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
		-device virtio-net-pci,netdev=net0,mac=52:54:00:12:34:56 \
		-cdrom $(IMAGE_NAME).iso

.PHONY: libc-headers
libc-headers:
	cd libc; meson setup --prefix=$(SYSROOT_DIR)/usr --cross-file=../meta/crossfile.txt -Dheaders_only=true build
	cd libc/build; ninja install

.PHONY: toolchain
toolchain: libc-headers
	./toolchain/build_gcc.sh
	./toolchain/build_qemu.sh

.PHONY: todolist
todolist:
	@echo -e "List of todos and fixme in sources: \n"
	-@grep -FHr -e TODO -e FIXME kernel

limine/limine:
	$(MAKE) -C limine CC="cc" CFLAGS="-O2 -pipe"

.PHONY: kernel
kernel:
	$(RM) -r kernel/src/dev/acpi/uacpi/tests
	$(MAKE) -C kernel

.PHONY: libc
libc:
	cd libc; meson setup --prefix=$(SYSROOT_DIR)/usr --cross-file=../meta/crossfile.txt -Dno_headers=true -Ddefault_library=static build
	cd libc/build; ninja install

.PHONY: userspace
userspace:
	$(MAKE) -C userspace

.PHONY: initrd
initrd:
	cd $(SYSROOT_DIR); tar -cf ../$(INITRD_FILE) *

.NOTPARALLEL:
$(IMAGE_NAME).iso: limine/limine kernel libc userspace initrd
	rm -rf iso_root
	mkdir -p iso_root/boot
	cp -v kernel/$(KERNEL_FILE) $(INITRD_FILE) iso_root/boot/
	mkdir -p iso_root/boot/limine
	cp -v meta/limine.conf iso_root/boot/limine/
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
	$(RM) -r iso_root libc/build $(IMAGE_NAME).iso $(INITRD_FILE)
	$(MAKE) -C userspace clean
	$(MAKE) -C kernel clean

.PHONY: distclean
distclean:
	$(MAKE) -C limine clean
	$(MAKE) -C kernel distclean
