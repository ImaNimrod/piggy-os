hostname_exe = executable(
  'hostname',
  'hostname.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'hostname.1',
  install_dir: get_option('mandir') / 'man1'
)
