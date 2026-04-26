mv_exe = executable(
  'mv',
  'mv.c',
  install: true,
  install_dir: get_option('bindir'),
  dependencies: shared_dep
)

install_data(
  'mv.1',
  install_dir: get_option('mandir') / 'man1'
)
