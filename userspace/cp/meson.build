cp_exe = executable(
  'cp',
  'cp.c',
  dependencies: shared_dep,
  install: true,
  install_dir: get_option('bindir'),
)

install_data(
  'cp.1',
  install_dir: get_option('mandir') / 'man1'
)
