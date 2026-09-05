tree_exe = executable(
  'tree',
  'tree.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'tree.1',
  install_dir: get_option('mandir') / 'man1'
)
