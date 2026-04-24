truncate_exe = executable(
  'truncate',
  'truncate.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'truncate.1',
  install_dir: get_option('mandir') / 'man1'
)
