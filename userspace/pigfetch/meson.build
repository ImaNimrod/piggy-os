pigfetch_exe = executable(
  'pigfetch',
  'pigfetch.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'pigfetch.1',
  install_dir: get_option('mandir') / 'man1'
)
