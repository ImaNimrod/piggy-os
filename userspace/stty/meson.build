stty_exe = executable(
  'stty',
  'stty.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'stty.1',
  install_dir: get_option('mandir') / 'man1'
)
