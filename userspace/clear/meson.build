clear_exe = executable(
  'clear',
  'clear.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'clear.1',
  install_dir: get_option('mandir') / 'man1'
)
