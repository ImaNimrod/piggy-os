sleep_exe = executable(
  'sleep',
  'sleep.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'sleep.1',
  install_dir: get_option('mandir') / 'man1'
)
