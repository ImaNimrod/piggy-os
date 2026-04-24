shuf_exe = executable(
  'shuf',
  'shuf.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'shuf.1',
  install_dir: get_option('mandir') / 'man1'
)
