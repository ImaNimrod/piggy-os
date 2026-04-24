wc_exe = executable(
  'wc',
  'wc.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'wc.1',
  install_dir: get_option('mandir') / 'man1'
)
