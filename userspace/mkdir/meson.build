mkdir_exe = executable(
  'mkdir',
  'mkdir.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'mkdir.1',
  install_dir: get_option('mandir') / 'man1'
)
