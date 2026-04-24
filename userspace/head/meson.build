head_exe = executable(
  'head',
  'head.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'head.1',
  install_dir: get_option('mandir') / 'man1'
)
