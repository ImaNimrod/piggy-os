seq_exe = executable(
  'seq',
  'seq.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'seq.1',
  install_dir: get_option('mandir') / 'man1'
)
