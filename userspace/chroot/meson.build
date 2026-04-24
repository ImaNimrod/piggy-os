chroot_exe = executable(
  'chroot',
  'chroot.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'chroot.1',
  install_dir: get_option('mandir') / 'man1'
)
