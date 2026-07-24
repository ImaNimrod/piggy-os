mv_exe = executable(
  'mv',
  'mv.c',
  dependencies: shared_dep,
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'mv.1',
  install_dir: get_option('mandir') / 'man1'
)
