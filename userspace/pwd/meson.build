pwd_exe = executable(
  'pwd',
  'pwd.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'pwd.1',
  install_dir: get_option('mandir') / 'man1'
)
