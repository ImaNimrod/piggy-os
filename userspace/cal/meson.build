cal_exe = executable(
  'cal',
  'cal.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'cal.1',
  install_dir: get_option('mandir') / 'man1'
)
