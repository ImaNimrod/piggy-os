printf_exe = executable(
  'printf',
  'printf.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'printf.1',
  install_dir: get_option('mandir') / 'man1'
)
