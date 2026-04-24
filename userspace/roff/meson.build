roff_exe = executable(
  'roff',
  'roff.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'roff.1',
  install_dir: get_option('mandir') / 'man1'
)
