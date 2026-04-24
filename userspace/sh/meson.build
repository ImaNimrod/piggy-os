sh_sources = files(
  'builtins.c',
  'history.c',
  'sh.c',
)

sh_exe = executable(
  'sh',
  sh_sources,
  install: true,
  install_dir: get_option('bindir')
)

install_data(
  'sh.1',
  install_dir: get_option('mandir') / 'man1'
)
