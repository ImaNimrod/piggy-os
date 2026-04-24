rmdir_exe = executable(
  'rmdir',
  'rmdir.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'rmdir.1',
  install_dir: get_option('mandir') / 'man1'
)
