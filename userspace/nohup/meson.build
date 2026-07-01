nohup_exe = executable(
  'nohup',
  'nohup.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'nohup.1',
  install_dir: get_option('mandir') / 'man1'
)
