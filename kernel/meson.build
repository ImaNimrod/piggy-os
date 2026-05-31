project(
  'kernel',
  'c',
  default_options: ['c_std=gnu99', 'debug=true', 'optimization=2', 'warning_level=2'],
)

git_commit = run_command('git', 'describe', '--always', '--tags', check: true).stdout().strip()

add_global_arguments([
  '-fdata-sections',
  '-ffreestanding',
  '-ffunction-sections',
  '-fms-extensions',
  '-fstack-protector-strong',
  '-fno-stack-check',
  '-fno-omit-frame-pointer',
  '-fno-pic',

  '-Winline',
  '-Wnested-externs',
  '-Wredundant-decls',
  '-Wshadow',
  '-Wstrict-prototypes',

  '-m64',
  '-march=x86-64',
  '-mcmodel=kernel',
  '-mgeneral-regs-only',
  '-mno-80387',
  '-mno-mmx',
  '-mno-red-zone',
  '-mno-sse',
  '-mno-sse2',

  '-DGIT_COMMIT="@0@"'.format(git_commit),
  '-DLIMINE_API_REVISION=3',
  '-DPRINTF_DISABLE_SUPPORT_EXPONENTIAL',
  '-DPRINTF_DISABLE_SUPPORT_FLOAT',
  '-DFLANTERM_FB_DISABLE_BUMP_ALLOC',
  '-DUACPI_KERNEL_INITIALIZATION',
  '-DUACPI_NATIVE_ALLOC_ZEROED',
  '-DUACPI_OVERRIDE_LIBC',
  '-MMD',
  '-MP',
], language: ['c'])

add_global_link_arguments([
  '-nostdlib',
  '-static',
  '-T' + meson.current_source_dir() / 'linker.ld',
], language: ['c'])

kernel_sources = files(
  'src/main.c',

  'src/cpu/context.S',
  'src/cpu/gdt.c',
  'src/cpu/idt.c',
  'src/cpu/int.S',
  'src/cpu/ioapic.c',
  'src/cpu/isr.c',
  'src/cpu/lapic.c',
  'src/cpu/smp.c',
  'src/cpu/syscall.S',
  'src/cpu/tsc.c',

  'src/dev/acpi/acpi.c',
  'src/dev/acpi/uacpi_kernel_api.c',

  'src/dev/block/ahci/controller.c',
  'src/dev/block/ahci/device.c',

  'src/dev/block/nvme/controller.c',
  'src/dev/block/nvme/namespace.c',

  'src/dev/block/block.c',
  'src/dev/block/virtio_blk.c',

  'src/dev/char/console.c',
  'src/dev/char/fb.c',
  'src/dev/char/pseudo.c',
  'src/dev/char/tty.c',

  'src/dev/net/e1000.c',
  'src/dev/net/loopback.c',
  'src/dev/net/virtio_net.c',

  'src/dev/ps2/keyboard.c',
  'src/dev/ps2/mouse.c',
  'src/dev/ps2/ps2.c',

  'src/dev/usb/xhci/controller.c',
  'src/dev/usb/xhci/device.c',
  'src/dev/usb/xhci/ring.c',

  'src/dev/cmos.c',
  'src/dev/hpet.c',
  'src/dev/pci.c',
  'src/dev/pvclock.c',
  'src/dev/serial.c',
  'src/dev/virtio.c',

  'src/fs/devfs.c',
  'src/fs/fat32.c',
  'src/fs/file.c',
  'src/fs/initrd.c',
  'src/fs/pipe.c',
  'src/fs/tmpfs.c',
  'src/fs/vfs.c',

  'src/mem/paging.c',
  'src/mem/pmm.c',
  'src/mem/slab.c',
  'src/mem/vmm.c',

  'src/net/arp.c',
  'src/net/eth.c',
  'src/net/ipv4.c',
  'src/net/netif.c',
  'src/net/packet.c',

  'src/sys/syscalls/sys_archctl.c',
  'src/sys/syscalls/sys_chdir.c',
  'src/sys/syscalls/sys_chroot.c',
  'src/sys/syscalls/sys_close.c',
  'src/sys/syscalls/sys_dup.c',
  'src/sys/syscalls/sys_exec.c',
  'src/sys/syscalls/sys_exit.c',
  'src/sys/syscalls/sys_fcntl.c',
  'src/sys/syscalls/sys_fork.c',
  'src/sys/syscalls/sys_futex.c',
  'src/sys/syscalls/sys_getclock.c',
  'src/sys/syscalls/sys_getdents.c',
  'src/sys/syscalls/sys_getpid.c',
  'src/sys/syscalls/sys_getppid.c',
  'src/sys/syscalls/sys_gettid.c',
  'src/sys/syscalls/sys_ioctl.c',
  'src/sys/syscalls/sys_mkdir.c',
  'src/sys/syscalls/sys_mmap.c',
  'src/sys/syscalls/sys_mount.c',
  'src/sys/syscalls/sys_mprotect.c',
  'src/sys/syscalls/sys_munmap.c',
  'src/sys/syscalls/sys_open.c',
  'src/sys/syscalls/sys_pipe.c',
  'src/sys/syscalls/sys_poll.c',
  'src/sys/syscalls/sys_poweroff.c',
  'src/sys/syscalls/sys_pread.c',
  'src/sys/syscalls/sys_pwrite.c',
  'src/sys/syscalls/sys_read.c',
  'src/sys/syscalls/sys_rename.c',
  'src/sys/syscalls/sys_seek.c',
  'src/sys/syscalls/sys_setclock.c',
  'src/sys/syscalls/sys_sleep.c',
  'src/sys/syscalls/sys_stat.c',
  'src/sys/syscalls/sys_sync.c',
  'src/sys/syscalls/sys_threadexit.c',
  'src/sys/syscalls/sys_threadnew.c',
  'src/sys/syscalls/sys_truncate.c',
  'src/sys/syscalls/sys_uname.c',
  'src/sys/syscalls/sys_unlink.c',
  'src/sys/syscalls/sys_unmount.c',
  'src/sys/syscalls/sys_utime.c',
  'src/sys/syscalls/sys_wait.c',
  'src/sys/syscalls/sys_write.c',

  'src/sys/elf.c',
  'src/sys/process.c',
  'src/sys/scheduler.c',
  'src/sys/syscall.c',
  'src/sys/timer.c',

  'src/utils/flanterm/src/flanterm_backends/fb.c',
  'src/utils/flanterm/src/flanterm.c',

  'src/utils/printf/printf.c',

  'src/utils/cmdline.c',
  'src/utils/event.c',
  'src/utils/hashmap.c',
  'src/utils/log.c',
  'src/utils/mutex.c',
  'src/utils/random.c',
  'src/utils/semaphore.c',
  'src/utils/spinlock.c',
  'src/utils/string.c',
  'src/utils/usercopy.c',
  'src/utils/vector.c',
  'src/utils/wait_queue.c',
)

kernel_includes = [include_directories('include')]

uacpi = subproject('uacpi')

uacpi_sources = uacpi.get_variable('sources')
kernel_sources += uacpi_sources

uacpi_includes = uacpi.get_variable('includes')
kernel_includes += uacpi_includes

executable(
  'kernel.elf',
  kernel_sources,
  include_directories: kernel_includes,
  install: true,
)
