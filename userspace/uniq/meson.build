uniq_exe = executable(
  'uniq',
  'uniq.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'uniq.1',
  install_dir: get_option('mandir') / 'man1'
)
