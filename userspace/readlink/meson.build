readlink_exe = executable(
  'readlink',
  'readlink.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'readlink.1',
  install_dir: get_option('mandir') / 'man1'
)
