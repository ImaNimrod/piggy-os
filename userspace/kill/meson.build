kill_exe = executable(
  'kill',
  'kill.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'kill.1',
  install_dir: get_option('mandir') / 'man1'
)
