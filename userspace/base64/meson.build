base64_exe = executable(
  'base64',
  'base64.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'base64.1',
  install_dir: get_option('mandir') / 'man1'
)
