project(
  'kernel',
  'c',
  default_options: ['c_std=gnu23', 'debug=true', 'optimization=2', 'warning_level=2'],
)

git_commit = run_command('git', 'describe', '--always', '--tags', check: true).stdout().strip()

add_global_arguments([
  '-fdata-sections',
  '-ffreestanding',
  '-ffunction-sections',
  '-fms-extensions',
  '-fno-builtin',
  '-fno-omit-frame-pointer',
  '-fno-pic',
  '-fno-stack-check',
  '-fno-strict-aliasing',
  '-fstack-protector-strong',

  '-Wnested-externs',
  '-Wold-style-definition',
  '-Wredundant-decls',
  '-Wshadow',
  '-Wstrict-prototypes',
  '-Wvla',

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
  '-DPRINTF_DISABLE_SUPPORT_FLOAT',
  '-DPRINTF_DISABLE_SUPPORT_EXPONENTIAL',
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
  '-Wl,-T,' + meson.current_source_dir() / 'linker.ld',
  '-Wl,--gc-sections',
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
  'src/dev/pit.c',
  'src/dev/pvclock.c',
  'src/dev/serial.c',
  'src/dev/virtio.c',

  'src/fs/devfs.c',
  'src/fs/fat32.c',
  'src/fs/file.c',
  'src/fs/initrd.c',
  'src/fs/pipe.c',
  'src/fs/poll.c',
  'src/fs/socket.c',
  'src/fs/tmpfs.c',
  'src/fs/vfs.c',

  'src/mem/paging.c',
  'src/mem/pmm.c',
  'src/mem/slab.c',
  'src/mem/vmm.c',

  'src/net/arp.c',
  'src/net/eth.c',
  'src/net/icmp.c',
  'src/net/ipv4.c',
  'src/net/netif.c',
  'src/net/raw.c',
  'src/net/udp.c',

  'src/sys/syscalls/sys_archctl.c',
  'src/sys/syscalls/sys_bind.c',
  'src/sys/syscalls/sys_chdir.c',
  'src/sys/syscalls/sys_chroot.c',
  'src/sys/syscalls/sys_close.c',
  'src/sys/syscalls/sys_connect.c',
  'src/sys/syscalls/sys_dup.c',
  'src/sys/syscalls/sys_exec.c',
  'src/sys/syscalls/sys_exit.c',
  'src/sys/syscalls/sys_fcntl.c',
  'src/sys/syscalls/sys_fork.c',
  'src/sys/syscalls/sys_futex.c',
  'src/sys/syscalls/sys_getclock.c',
  'src/sys/syscalls/sys_getclockres.c',
  'src/sys/syscalls/sys_getdents.c',
  'src/sys/syscalls/sys_getpeername.c',
  'src/sys/syscalls/sys_getpgid.c',
  'src/sys/syscalls/sys_getpid.c',
  'src/sys/syscalls/sys_getppid.c',
  'src/sys/syscalls/sys_getsockname.c',
  'src/sys/syscalls/sys_gettid.c',
  'src/sys/syscalls/sys_ioctl.c',
  'src/sys/syscalls/sys_kill.c',
  'src/sys/syscalls/sys_link.c',
  'src/sys/syscalls/sys_mkdir.c',
  'src/sys/syscalls/sys_mmap.c',
  'src/sys/syscalls/sys_mount.c',
  'src/sys/syscalls/sys_mprotect.c',
  'src/sys/syscalls/sys_munmap.c',
  'src/sys/syscalls/sys_open.c',
  'src/sys/syscalls/sys_pipe.c',
  'src/sys/syscalls/sys_poll.c',
  'src/sys/syscalls/sys_powerctl.c',
  'src/sys/syscalls/sys_pread.c',
  'src/sys/syscalls/sys_procctl.c',
  'src/sys/syscalls/sys_pwrite.c',
  'src/sys/syscalls/sys_read.c',
  'src/sys/syscalls/sys_readlink.c',
  'src/sys/syscalls/sys_recv.c',
  'src/sys/syscalls/sys_rename.c',
  'src/sys/syscalls/sys_seek.c',
  'src/sys/syscalls/sys_send.c',
  'src/sys/syscalls/sys_setclock.c',
  'src/sys/syscalls/sys_sethostname.c',
  'src/sys/syscalls/sys_setpgid.c',
  'src/sys/syscalls/sys_shutdown.c',
  'src/sys/syscalls/sys_sigaction.c',
  'src/sys/syscalls/sys_sigaltstack.c',
  'src/sys/syscalls/sys_sigpending.c',
  'src/sys/syscalls/sys_sigprocmask.c',
  'src/sys/syscalls/sys_sigreturn.c',
  'src/sys/syscalls/sys_sigsuspend.c',
  'src/sys/syscalls/sys_sleep.c',
  'src/sys/syscalls/sys_socket.c',
  'src/sys/syscalls/sys_stat.c',
  'src/sys/syscalls/sys_symlink.c',
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
  'src/sys/syscalls/sys_yield.c',

  'src/sys/elf.c',
  'src/sys/process.c',
  'src/sys/scheduler.c',
  'src/sys/signal.c',
  'src/sys/syscall.c',
  'src/sys/timer.c',

  'src/utils/cmdline.c',
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

flanterm = subproject('flanterm')
pdgzip = subproject('pdgzip')
printf = subproject('printf')
uacpi = subproject('uacpi')

kernel_sources += flanterm.get_variable('sources')
kernel_sources += pdgzip.get_variable('sources')
kernel_sources += printf.get_variable('sources')
kernel_sources += uacpi.get_variable('sources')

kernel_includes += flanterm.get_variable('includes')
kernel_includes += pdgzip.get_variable('includes')
kernel_includes += printf.get_variable('includes')
kernel_includes += uacpi.get_variable('includes')

executable(
  'kernel.elf',
  kernel_sources,
  include_directories: kernel_includes,
  install: true,
)
