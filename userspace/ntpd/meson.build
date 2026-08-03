ntpd_exe = executable(
  'ntpd',
  'ntpd.c',
  dependencies: libconfig_dep,
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'ntpd.8',
  install_dir: get_option('mandir') / 'man8'
)

install_data(
  'ntpd.conf.5',
  install_dir: get_option('mandir') / 'man5'
)
