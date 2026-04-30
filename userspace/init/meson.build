init_exe = executable(
  'init',
  'init.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'init.8',
  install_dir: get_option('mandir') / 'man8'
)
