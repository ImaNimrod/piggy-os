ln_exe = executable(
  'ln',
  'ln.c',
  install: true,
  install_dir: get_option('bindir'),
  dependencies: shared_dep
)

install_data(
  'ln.1',
  install_dir: get_option('mandir') / 'man1'
)
