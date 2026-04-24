mount_exe = executable(
  'hwclock',
  'hwclock.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'hwclock.8',
  install_dir: get_option('mandir') / 'man8'
)
