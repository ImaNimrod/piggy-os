echo_exe = executable(
  'echo',
  'echo.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'echo.1',
  install_dir: get_option('mandir') / 'man1'
)
