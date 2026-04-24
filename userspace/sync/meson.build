sync_exe = executable(
  'sync',
  'sync.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'sync.1',
  install_dir: get_option('mandir') / 'man1'
)
