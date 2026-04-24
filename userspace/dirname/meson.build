dirname_exe = executable(
  'dirname',
  'dirname.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'dirname.1',
  install_dir: get_option('mandir') / 'man1'
)
