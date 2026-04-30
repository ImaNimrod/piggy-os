realpath_exe = executable(
  'realpath',
  'realpath.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'realpath.1',
  install_dir: get_option('mandir') / 'man1'
)
