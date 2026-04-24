env_exe = executable(
  'env',
  'env.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'env.1',
  install_dir: get_option('mandir') / 'man1'
)
