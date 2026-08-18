xargs_exe = executable(
  'xargs',
  'xargs.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'xargs.1',
  install_dir: get_option('mandir') / 'man1'
)
