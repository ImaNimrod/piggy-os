mount_exe = executable(
  'mount',
  'mount.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'mount.8',
  install_dir: get_option('mandir') / 'man8'
)
