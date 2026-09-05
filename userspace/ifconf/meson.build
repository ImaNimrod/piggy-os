ifconf_exe = executable(
  'ifconf',
  'ifconf.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'ifconf.8',
  install_dir: get_option('mandir') / 'man8'
)
