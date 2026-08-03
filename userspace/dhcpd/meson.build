dhcpd_exe = executable(
  'dhcpd',
  'dhcpd.c',
  dependencies: libconfig_dep,
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'dhcpd.8',
  install_dir: get_option('mandir') / 'man8'
)

install_data(
  'dhcpd.conf.5',
  install_dir: get_option('mandir') / 'man5'
)
