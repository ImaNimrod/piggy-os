unmount_exe = executable(
  'unmount',
  'unmount.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'unmount.8',
  install_dir: get_option('mandir') / 'man8'
)
