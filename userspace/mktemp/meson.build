mktemp_exe = executable(
  'mktemp',
  'mktemp.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'mktemp.1',
  install_dir: get_option('mandir') / 'man1'
)
