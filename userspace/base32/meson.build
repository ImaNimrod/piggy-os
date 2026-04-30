base32_exe = executable(
  'base32',
  'base32.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'base32.1',
  install_dir: get_option('mandir') / 'man1'
)
