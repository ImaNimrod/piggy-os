touch_exe = executable(
  'touch',
  'touch.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'touch.1',
  install_dir: get_option('mandir') / 'man1'
)
