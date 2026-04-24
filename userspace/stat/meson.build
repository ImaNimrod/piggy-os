stat_exe = executable(
  'stat',
  'stat.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'stat.1',
  install_dir: get_option('mandir') / 'man1'
)
