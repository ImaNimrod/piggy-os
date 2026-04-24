ls_exe = executable(
  'ls',
  'ls.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'ls.1',
  install_dir: get_option('mandir') / 'man1'
)
