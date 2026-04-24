tail_exe = executable(
  'tail',
  'tail.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'tail.1',
  install_dir: get_option('mandir') / 'man1'
)
