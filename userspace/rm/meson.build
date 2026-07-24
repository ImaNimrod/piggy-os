rm_exe = executable(
  'rm',
  'rm.c',
  dependencies: shared_dep,
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'rm.1',
  install_dir: get_option('mandir') / 'man1'
)
