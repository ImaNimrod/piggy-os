ln_exe = executable(
  'ln',
  'ln.c',
  dependencies: shared_dep,
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'ln.1',
  install_dir: get_option('mandir') / 'man1'
)
