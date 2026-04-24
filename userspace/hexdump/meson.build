hexdump_exe = executable(
  'hexdump',
  'hexdump.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'hexdump.1',
  install_dir: get_option('mandir') / 'man1'
)
