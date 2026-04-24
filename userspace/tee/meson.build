tee_exe = executable(
  'tee',
  'tee.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'tee.1',
  install_dir: get_option('mandir') / 'man1'
)
