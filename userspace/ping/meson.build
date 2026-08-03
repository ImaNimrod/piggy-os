ping_exe = executable(
  'ping',
  'ping.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'ping.8',
  install_dir: get_option('mandir') / 'man8'
)
