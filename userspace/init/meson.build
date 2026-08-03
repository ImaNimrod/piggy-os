init_exe = executable(
  'init',
  'init.c',
  dependencies: libconfig_dep,
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'reboot',
  install_dir: get_option('bindir')
)

install_data(
  'shutdown',
  install_dir: get_option('bindir')
)

install_data(
  'init.8',
  install_dir: get_option('mandir') / 'man8'
)

install_data(
  'rc.conf.5',
  install_dir: get_option('mandir') / 'man5'
)
