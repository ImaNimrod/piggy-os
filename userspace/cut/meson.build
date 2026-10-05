cut_exe = executable(
  'cut',
  'cut.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'cut.1',
  install_dir: get_option('mandir') / 'man1'
)
