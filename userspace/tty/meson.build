tty_exe = executable(
  'tty',
  'tty.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'tty.1',
  install_dir: get_option('mandir') / 'man1'
)
