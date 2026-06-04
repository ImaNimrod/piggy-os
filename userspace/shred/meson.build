shred_exe = executable(
  'shred',
  'shred.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'shred.1',
  install_dir: get_option('mandir') / 'man1'
)
