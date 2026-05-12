uptime_exe = executable(
  'uptime',
  'uptime.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'uptime.1',
  install_dir: get_option('mandir') / 'man1'
)
