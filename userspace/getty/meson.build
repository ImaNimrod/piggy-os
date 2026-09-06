getty_exe = executable(
  'getty',
  'getty.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'getty.1',
  install_dir: get_option('mandir') / 'man1'
)
