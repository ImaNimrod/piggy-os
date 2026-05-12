include ./config.mk

EMUOPTS := -machine q35 \
		   -m 4G \
		   -cpu host \
		   -enable-kvm \
		   -smp 2 \
		   -no-reboot \
		   -serial stdio \
		   -rtc base=utc \
		   -bios edk2-ovmf/ovmf-code-x86_64.fd

NPROC := $(patsubst -j%,%,$(filter -j%,$(MAKEFLAGS)))
ifeq ($(NPROC),)
	NPROC := 1
endif

export PATH := $(PATH):$(TOOLCHAIN_DIR)/local/bin

.PHONY: all
all: piggy.iso

.PHONY: run
run: run-virtio

.PHONY: run-minimal
run-minimal: edk2-ovmf
	$(EMU) $(EMUOPTS) \
		-nic none \
		-cdrom piggy.iso

.PHONY: run-realhw
run-realhw: edk2-ovmf
	$(EMU) $(EMUOPTS) \
		-drive file=disk.img,format=raw,if=none,id=disk \
		-device nvme,drive=disk,serial=12345678 \
		-netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
		-device e1000e,netdev=net0,mac=52:54:00:12:34:56 \
		-cdrom piggy.iso

.PHONY: run-virtio
run-virtio: edk2-ovmf
	$(EMU) $(EMUOPTS) \
		-drive id=disk,file=disk.img,format=raw,if=none -device virtio-blk-pci,drive=disk \
		-netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
		-device virtio-net-pci,netdev=net0,mac=52:54:00:12:34:56 \
		-cdrom piggy.iso

.PHONY: toolchain
toolchain:
	./toolchain/build_gcc.sh
	./toolchain/build_qemu.sh

.PHONY: todolist
todolist:
	@echo -e "List of todos and fixme in sources: \n"
	@git grep -e TODO -e FIXME

.PHONY: kernel
kernel: kernel/build
	cd kernel; meson compile --jobs $(NPROC) -C build

kernel/build:
	cd kernel; meson setup --cross-file=../toolchain/meson-crossfile.txt build

.PHONY: libc
libc: libc/build
	cd libc; meson compile --jobs $(NPROC) -C build
	cd libc; meson install -C build

libc/build:
	cd libc; meson setup --prefix=$(SYSROOT_DIR)/usr --cross-file=../toolchain/meson-crossfile.txt -Dheaders_only=false build

.PHONY: userspace
userspace: userspace/build
	cd userspace; meson compile --jobs $(NPROC) -C build
	cd userspace; meson install -C build

userspace/build:
	cd userspace; meson setup --prefix=$(SYSROOT_DIR)/usr --cross-file=../toolchain/meson-crossfile.txt build

.PHONY: initrd
initrd:
	cd $(SYSROOT_DIR); tar -cf ../initrd.tar *

.NOTPARALLEL:
piggy.iso: limine-binary/limine kernel libc userspace initrd
	rm -rf iso_root
	mkdir -p iso_root/boot
	cp -v kernel/build/kernel.elf initrd.tar iso_root/boot/
	mkdir -p iso_root/boot/limine
	cp -v meta/limine.conf iso_root/boot/limine/
	mkdir -p iso_root/EFI/BOOT
	cp -v limine-binary/limine-bios.sys limine-binary/limine-bios-cd.bin limine-binary/limine-uefi-cd.bin iso_root/boot/limine/
	cp -v limine-binary/BOOTX64.EFI iso_root/EFI/BOOT/
	cp -v limine-binary/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o piggy.iso
	./limine-binary/limine bios-install piggy.iso
	rm -rf iso_root

edk2-ovmf:
	curl -L https://github.com/osdev0/edk2-ovmf-nightly/releases/latest/download/edk2-ovmf.tar.gz | tar -xzf -

limine-binary/limine: limine-binary
	$(MAKE) -C limine-binary CC="cc" CFLAGS="-O2 -pipe"

limine-binary:
	curl -L https://github.com/Limine-Bootloader/Limine/releases/download/v12.2.0/limine-binary.tar.xz | tar -xJf -

.PHONY: clean
clean:
	$(RM) -r iso_root kernel/build libc/build userspace/build
	$(RM) piggy.iso initrd.tar
	$(MAKE) -C userspace clean

.PHONY: distclean
distclean:
	$(MAKE) -C limine-binary clean
