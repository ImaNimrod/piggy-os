basename_exe = executable(
  'basename',
  'basename.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'basename.1',
  install_dir: get_option('mandir') / 'man1'
)
