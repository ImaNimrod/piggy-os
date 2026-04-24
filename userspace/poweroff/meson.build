poweroff_exe = executable(
  'poweroff',
  'poweroff.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'poweroff.8',
  install_dir: get_option('mandir') / 'man8'
)
