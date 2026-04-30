expr_exe = executable(
  'expr',
  'expr.c',
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'expr.1',
  install_dir: get_option('mandir') / 'man1'
)
