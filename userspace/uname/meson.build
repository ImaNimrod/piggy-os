uname_exe = executable(
  'uname',
  'uname.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'uname.1',
  install_dir: get_option('mandir') / 'man1'
)
