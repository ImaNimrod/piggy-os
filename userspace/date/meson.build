date_exe = executable(
  'date',
  'date.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'date.1',
  install_dir: get_option('mandir') / 'man1'
)
