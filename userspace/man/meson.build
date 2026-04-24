man_exe = executable(
  'man',
  'man.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'man.1',
  install_dir: get_option('mandir') / 'man1'
)
