ps_exe = executable(
  'ps',
  'ps.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'ps.1',
  install_dir: get_option('mandir') / 'man1'
)
