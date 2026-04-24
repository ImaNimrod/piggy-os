which_exe = executable(
  'which',
  'which.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'which.1',
  install_dir: get_option('mandir') / 'man1'
)
