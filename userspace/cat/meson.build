cat_exe = executable(
  'cat',
  'cat.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'cat.1',
  install_dir: get_option('mandir') / 'man1'
)
